#include "query/optimizer/rules/filter_pushdown.hpp"

#include "query/planner/bound_expression/bound_expression.hpp"
#include "query/planner/bound_logical_plan.hpp"

#include <unordered_set>

namespace eugraph {
namespace optimizer {

bool isPenetrable(OptNodeType type) {
    switch (type) {
    case OptNodeType::Expand:
    case OptNodeType::VarLenExpand:
    case OptNodeType::PathBuild:
    case OptNodeType::Filter:
    case OptNodeType::Sort:
    case OptNodeType::Distinct:
        return true;
    // LIMIT and SKIP are NOT penetrable: Filter(Limit(n, X)) -> Limit(n, Filter(X))
    // and Filter(Skip(n, X)) -> Skip(n, Filter(X)) change which rows are
    // truncated/skipped, so the transformation is not semantics-preserving.
    case OptNodeType::Skip:
    case OptNodeType::Limit:
        return false;
    default:
        return false;
    }
}

namespace {

void collectColumnNames(const binder::BoundExpression& expr, std::unordered_set<std::string>& out) {
    std::visit(
        [&out](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, binder::BoundColumnRef>) {
                out.insert(val.name);
            } else if constexpr (std::is_same_v<T, binder::BoundVariableRef>) {
                out.insert(val.name);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryOp>>) {
                collectColumnNames(val->left, out);
                collectColumnNames(val->right, out);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnaryOp>>) {
                collectColumnNames(val->operand, out);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPropertyRef>>) {
                collectColumnNames(val->object, out);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDynamicPropertyRef>>) {
                collectColumnNames(val->object, out);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLabelCast>>) {
                collectColumnNames(val->object, out);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFunctionCall>>) {
                for (const auto& arg : val->args)
                    collectColumnNames(arg, out);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundList>>) {
                for (const auto& elem : val->elements)
                    collectColumnNames(elem, out);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCase>>) {
                if (val->subject)
                    collectColumnNames(*val->subject, out);
                for (const auto& [w, t] : val->when_thens) {
                    collectColumnNames(w, out);
                    collectColumnNames(t, out);
                }
                if (val->else_expr)
                    collectColumnNames(*val->else_expr, out);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSubscript>>) {
                collectColumnNames(val->list, out);
                collectColumnNames(val->index, out);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSlice>>) {
                collectColumnNames(val->list, out);
                if (val->from)
                    collectColumnNames(*val->from, out);
                if (val->to)
                    collectColumnNames(*val->to, out);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundMap>>) {
                for (const auto& [k, v] : val->entries) {
                    (void)k;
                    collectColumnNames(v, out);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundListComprehension>>) {
                if (!val->variable.empty())
                    out.insert(val->variable);
                collectColumnNames(val->list_expr, out);
                if (val->where_pred)
                    collectColumnNames(*val->where_pred, out);
                collectColumnNames(val->projection, out);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAllExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundAnyExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundNoneExpr>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSingleExpr>>) {
                if (!val->variable.empty())
                    out.insert(val->variable);
                collectColumnNames(val->list_expr, out);
                if (val->where_pred)
                    collectColumnNames(*val->where_pred, out);
            }
        },
        expr);
}

/// Returns the set of variable names that the given operator newly introduces
/// (i.e. columns it creates, excluding columns passed through from its child).
/// Pushing a Filter that references any of these names below the operator
/// would leave those references dangling because the columns don't exist yet.
std::unordered_set<std::string> introducedVariableNames(const binder::BoundLogicalOperator& op) {
    std::unordered_set<std::string> introduced;
    std::visit(
        [&introduced](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                if (!val->edge_variable.empty())
                    introduced.insert(val->edge_variable);
                if (!val->dst_variable.empty())
                    introduced.insert(val->dst_variable);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                if (!val->dst_variable.empty())
                    introduced.insert(val->dst_variable);
                if (!val->path_variable.empty())
                    introduced.insert(val->path_variable);
                if (!val->edge_variable.empty())
                    introduced.insert(val->edge_variable);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPathBuildOp>>) {
                if (!val->path_variable.empty())
                    introduced.insert(val->path_variable);
            }
        },
        op);
    return introduced;
}

} // namespace

namespace {

bool canPushThroughPredicate(const binder::BoundExpression& predicate, const binder::BoundLogicalOperator& child_op) {
    OptNodeType child_type = nodeTypeFromVariantIndex(child_op.index());
    if (!isPenetrable(child_type))
        return false;

    // Pushing a Filter through an operator that introduces new variables is
    // only safe if the predicate does not reference any of those variables.
    auto introduced = introducedVariableNames(child_op);
    if (introduced.empty())
        return true;

    std::unordered_set<std::string> referenced;
    collectColumnNames(predicate, referenced);
    for (const auto& name : introduced) {
        if (referenced.count(name))
            return false;
    }
    return true;
}

} // namespace

bool FilterPushdownRule::condition(GroupExpr& expr, Memo& memo) const {
    if (expr.child_groups.empty())
        return false;

    Group& child_group = memo.getGroup(expr.child_groups[0]);
    if (child_group.logical_exprs.empty())
        return false;

    const auto& filter_op = std::get<std::unique_ptr<binder::BoundFilterOp>>(expr.op);
    for (ExprId child_eid : child_group.logical_exprs) {
        GroupExpr& child_expr = memo.getExpr(child_eid);
        if (canPushThroughPredicate(filter_op->predicate, child_expr.op))
            return true;
    }
    return false;
}

std::vector<std::unique_ptr<GroupExpr>> FilterPushdownRule::substitute(GroupExpr& expr, Memo& memo) const {
    // Single-step pushdown: Filter(Child(X)) → Child(Filter(X)), for every
    // equivalent Child expression in the child group that can be penetrated.
    auto& filter_op = std::get<std::unique_ptr<binder::BoundFilterOp>>(expr.op);
    auto predicate = cloneBoundExpression(filter_op->predicate);

    GroupId child_gid = expr.child_groups[0];
    Group& child_group = memo.getGroup(child_gid);

    std::vector<std::unique_ptr<GroupExpr>> results;
    for (ExprId child_eid : child_group.logical_exprs) {
        GroupExpr& child_expr = memo.getExpr(child_eid);
        if (!canPushThroughPredicate(filter_op->predicate, child_expr.op))
            continue;

        binder::BoundLogicalOperator child_op = cloneBoundLogicalOperator(child_expr.op);
        auto grandchild_groups = child_expr.child_groups;

        auto new_filter_op = std::make_unique<binder::BoundFilterOp>();
        new_filter_op->predicate = cloneBoundExpression(predicate);

        GroupExpr* filter_gexpr =
            memo.createGroupWithExpr(binder::BoundLogicalOperator(std::move(new_filter_op)), grandchild_groups);

        std::vector<GroupId> new_child_groups = {filter_gexpr->group_id};
        results.push_back(std::make_unique<GroupExpr>(INVALID_EXPR_ID, expr.group_id, std::move(child_op),
                                                      std::move(new_child_groups)));
    }
    return results;
}

} // namespace optimizer
} // namespace eugraph
