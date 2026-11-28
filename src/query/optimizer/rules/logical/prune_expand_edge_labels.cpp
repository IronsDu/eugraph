#include "query/optimizer/rules/logical/prune_expand_edge_labels.hpp"

#include "query/optimizer/framework/memo.hpp"
#include "query/planner/logical_plan/operator/bound_expand_op.hpp"
#include "query/planner/logical_plan/operator/bound_filter_op.hpp"

#include <algorithm>

namespace eugraph {
namespace optimizer {

namespace {

/// Variable + declared type of the property access object.
bool propertyObject(const binder::BoundExpression& object, std::string& var, const binder::BoundType*& type) {
    if (const auto* column = std::get_if<binder::BoundColumnRef>(&object)) {
        var = column->name;
        type = &column->type;
        return true;
    }
    if (const auto* variable = std::get_if<binder::BoundVariableRef>(&object)) {
        var = variable->name;
        type = &variable->type;
        return true;
    }
    return false;
}

} // namespace

PatternNode PruneExpandEdgeLabelsRule::pattern() const {
    return {OptNodeType::Filter, {{}}};
}

std::vector<std::unique_ptr<GroupExpr>> PruneExpandEdgeLabelsRule::substitute(GroupExpr& expr, Memo& memo) const {
    const auto* filter = std::get_if<std::unique_ptr<binder::BoundFilterOp>>(&expr.op);
    if (!filter || !*filter || expr.child_groups.empty())
        return {};

    // Predicate must be exactly `edgeVar.prop IS NOT NULL`.
    const auto* unary = std::get_if<std::unique_ptr<binder::BoundUnaryOp>>(&(*filter)->predicate);
    if (!unary || !*unary || (*unary)->op != cypher::UnaryOperator::IS_NOT_NULL)
        return {};
    const auto* property = std::get_if<std::unique_ptr<binder::BoundPropertyRef>>(&(*unary)->operand);
    if (!property || !*property || (*property)->property_name.empty())
        return {};

    std::string var;
    const binder::BoundType* type = nullptr;
    if (!propertyObject((*property)->object, var, type) || var.empty())
        return {};
    if (!type || (type->kind != binder::BoundTypeKind::EDGE && type->kind != binder::BoundTypeKind::EDGE_KEY))
        return {};

    // Child group must contain exactly one Expand expression on that edge.
    Group& child_group = memo.getGroup(expr.child_groups[0]);
    if (child_group.logical_exprs.size() != 1)
        return {};
    GroupExpr& child_expr = memo.getExpr(child_group.logical_exprs[0]);
    const auto* expand = std::get_if<std::unique_ptr<binder::BoundExpandOp>>(&child_expr.op);
    if (!expand || !*expand || child_expr.child_groups.size() != 1)
        return {};
    if ((*expand)->edge_variable != var)
        return {};
    if (!(*expand)->all_edge_labels && (*expand)->edge_label_ids.empty())
        return {};

    const catalog::Catalog* catalog = memo.getCatalog();
    if (catalog == nullptr)
        return {};

    // Labels that declare the property (edges are strong-only, so the binder
    // candidates are authoritative; verify against the catalog anyway).
    std::vector<EdgeLabelId> declaring;
    for (const auto& [edge_label_id, edge_label_def] : catalog->allEdgeLabels()) {
        bool declares = false;
        for (const auto& property_def : edge_label_def.properties) {
            if (property_def.name == (*property)->property_name) {
                declares = true;
                break;
            }
        }
        if (!declares)
            continue;
        if (!(*property)->candidates.empty()) {
            bool candidate = false;
            for (const auto& resolved : (*property)->candidates) {
                if (static_cast<EdgeLabelId>(resolved.label_id) == edge_label_id) {
                    candidate = true;
                    break;
                }
            }
            if (!candidate)
                continue;
        }
        declaring.push_back(edge_label_id);
    }
    if (declaring.empty())
        return {};

    // Narrow only; a predicate that would select every edge label is a no-op.
    std::vector<EdgeLabelId> target;
    if ((*expand)->edge_label_ids.empty()) {
        target = declaring;
        if (target.size() == catalog->allEdgeLabels().size())
            return {};
    } else {
        for (EdgeLabelId label : (*expand)->edge_label_ids) {
            if (std::find(declaring.begin(), declaring.end(), label) != declaring.end())
                target.push_back(label);
        }
        if (target.empty() || target == (*expand)->edge_label_ids)
            return {};
    }

    auto pruned = std::get<std::unique_ptr<binder::BoundExpandOp>>(cloneBoundLogicalOperator(child_expr.op));
    if (!pruned)
        return {};
    pruned->edge_label_ids = std::move(target);
    pruned->all_edge_labels = false;

    std::vector<GroupId> children{child_expr.child_groups[0]};
    std::vector<std::unique_ptr<GroupExpr>> result;
    result.push_back(
        std::make_unique<GroupExpr>(INVALID_EXPR_ID, expr.group_id, std::move(pruned), std::move(children)));
    return result;
}

} // namespace optimizer
} // namespace eugraph
