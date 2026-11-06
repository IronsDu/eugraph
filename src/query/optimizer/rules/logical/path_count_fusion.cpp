#include "query/optimizer/rules/logical/path_count_fusion.hpp"

#include "query/dataset/row.hpp"
#include "query/planner/bound_expression/bound_function_call.hpp"
#include "query/planner/bound_expression/bound_variable_ref.hpp"
#include "query/planner/bound_logical_plan.hpp"
#include "query/planner/logical_plan/operator/bound_expand_op.hpp"
#include "query/planner/logical_plan/operator/bound_filter_op.hpp"
#include "query/planner/logical_plan/operator/bound_path_count_op.hpp"

namespace eugraph {
namespace optimizer {

namespace {

std::string argName(const binder::BoundExpression& expr) {
    if (auto* ref = std::get_if<binder::BoundColumnRef>(&expr))
        return ref->name;
    if (auto* ref = std::get_if<binder::BoundVariableRef>(&expr))
        return ref->name;
    return {};
}

const binder::BoundFunctionCall* edgeUniqueCall(const binder::BoundFilterOp& filter) {
    auto* call = std::get_if<std::unique_ptr<binder::BoundFunctionCall>>(&filter.predicate);
    if (!call || !*call || !(*call)->func_def || (*call)->func_def->name != "__edge_unique")
        return nullptr;
    return call->get();
}

bool countAcceptsPath(const binder::BoundAggregateOp::AggregateItem& agg, const std::string& final_dst) {
    if (agg.arguments.empty())
        return true; // count(*)
    if (agg.arguments.size() != 1)
        return false;
    const auto& arg = agg.arguments[0];
    if (argName(arg) == final_dst)
        return true;
    auto* lit = std::get_if<binder::BoundLiteral>(&arg);
    return lit != nullptr && !isNull(lit->value); // count(1), count('x'), ...
}

struct FusionShape {
    GroupExpr* expand_expr = nullptr;
    const binder::BoundExpandOp* final_expand = nullptr;
    std::vector<std::string> edge_names;
};

/// Find any child alternative that has the fuseable shape. Aggregate/Filter
/// groups can already contain several equivalent expressions, so this must
/// not require a single memo expression.
bool findShape(GroupExpr& expr, Memo& memo, const binder::BoundAggregateOp::AggregateItem& item, FusionShape& shape) {
    if (expr.child_groups.size() != 1)
        return false;
    Group& filter_group = memo.getGroup(expr.child_groups[0]);
    for (ExprId filter_eid : filter_group.logical_exprs) {
        GroupExpr& filter_expr = memo.getExpr(filter_eid);
        auto* filter = std::get_if<std::unique_ptr<binder::BoundFilterOp>>(&filter_expr.op);
        if (!filter || !*filter || filter_expr.child_groups.size() != 1 ||
            (*filter)->uniqueness_scope == binder::BoundFilterOp::UniquenessScope::CorrelatedSubplan)
            continue;
        const binder::BoundFunctionCall* unique = edgeUniqueCall(**filter);
        if (!unique || unique->args.size() < 2)
            continue;

        Group& expand_group = memo.getGroup(filter_expr.child_groups[0]);
        for (ExprId expand_eid : expand_group.logical_exprs) {
            GroupExpr& expand_expr = memo.getExpr(expand_eid);
            auto* expand = std::get_if<std::unique_ptr<binder::BoundExpandOp>>(&expand_expr.op);
            if (!expand || !*expand || expand_expr.child_groups.size() != 1)
                continue;
            const auto& final = **expand;
            if (final.direction != cypher::RelationshipDirection::LEFT_TO_RIGHT || !final.dst_label_ids.empty() ||
                !final.dst_label_prop_ids.empty() || !final.edge_prop_ids.empty())
                continue;
            if (!countAcceptsPath(item, final.dst_variable))
                continue;

            std::vector<std::string> edge_names;
            for (const auto& arg : unique->args)
                edge_names.push_back(argName(arg));
            if (edge_names.size() < 2 || edge_names.back() != final.edge_variable)
                continue;

            bool prefix_ok = true;
            if (edge_names.size() > 2) {
                prefix_ok = false;
                Group& prefix_group = memo.getGroup(expand_expr.child_groups[0]);
                for (ExprId prefix_eid : prefix_group.logical_exprs) {
                    GroupExpr& prefix_expr = memo.getExpr(prefix_eid);
                    auto* prefix_filter = std::get_if<std::unique_ptr<binder::BoundFilterOp>>(&prefix_expr.op);
                    if (!prefix_filter || !*prefix_filter)
                        continue;
                    const binder::BoundFunctionCall* prefix = edgeUniqueCall(**prefix_filter);
                    if (!prefix || prefix->args.size() != edge_names.size() - 1)
                        continue;
                    bool matches = true;
                    for (size_t i = 0; i + 1 < edge_names.size(); ++i) {
                        if (argName(prefix->args[i]) != edge_names[i]) {
                            matches = false;
                            break;
                        }
                    }
                    if (matches) {
                        prefix_ok = true;
                        break;
                    }
                }
            }
            if (!prefix_ok)
                continue;

            shape.expand_expr = &expand_expr;
            shape.final_expand = expand->get();
            shape.edge_names = std::move(edge_names);
            return true;
        }
    }
    return false;
}

} // namespace

bool PathCountFusionRule::condition(GroupExpr& expr, Memo& memo) const {
    auto* agg = std::get_if<std::unique_ptr<binder::BoundAggregateOp>>(&expr.op);
    if (!agg || !*agg || !(*agg)->group_keys.empty() || (*agg)->aggregates.size() != 1 ||
        (*agg)->output_names.size() != 1)
        return false;
    const auto& item = (*agg)->aggregates[0];
    if (!item.func_def || item.func_def->name != "count" || item.distinct || item.is_internal)
        return false;
    FusionShape shape;
    return findShape(expr, memo, item, shape);
}

std::vector<std::unique_ptr<GroupExpr>> PathCountFusionRule::substitute(GroupExpr& expr, Memo& memo) const {
    auto* agg = std::get_if<std::unique_ptr<binder::BoundAggregateOp>>(&expr.op);
    if (!agg || !*agg || (*agg)->aggregates.size() != 1 || (*agg)->output_names.size() != 1)
        return {};
    const auto& item = (*agg)->aggregates[0];
    FusionShape shape;
    if (!findShape(expr, memo, item, shape) || !shape.final_expand || !shape.expand_expr)
        return {};

    auto path_count = std::make_unique<binder::BoundPathCountOp>();
    path_count->source_variable = shape.final_expand->src_variable;
    path_count->dst_variable = shape.final_expand->dst_variable;
    path_count->previous_edge_variables.assign(shape.edge_names.begin(), shape.edge_names.end() - 1);
    path_count->edge_label_ids = shape.final_expand->edge_label_ids;
    path_count->output_name = (*agg)->output_names[0];

    std::vector<std::unique_ptr<GroupExpr>> result;
    result.push_back(std::make_unique<GroupExpr>(INVALID_EXPR_ID, expr.group_id,
                                                 binder::BoundLogicalOperator(std::move(path_count)),
                                                 std::vector<GroupId>{shape.expand_expr->child_groups[0]}));
    return result;
}

} // namespace optimizer
} // namespace eugraph
