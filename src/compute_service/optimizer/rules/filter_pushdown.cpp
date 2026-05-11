#include "compute_service/optimizer/rules/filter_pushdown.hpp"

#include "compute_service/planner/bound_logical_plan.hpp"

namespace eugraph {
namespace optimizer {

bool isPenetrable(OptNodeType type) {
    switch (type) {
    case OptNodeType::Expand:
    case OptNodeType::VarLenExpand:
    case OptNodeType::PathBuild:
    case OptNodeType::Filter:
    case OptNodeType::Sort:
    case OptNodeType::Skip:
    case OptNodeType::Limit:
    case OptNodeType::Distinct:
        return true;
    default:
        return false;
    }
}

bool FilterPushdownRule::condition(GroupExpr& expr, Memo& memo) const {
    if (expr.child_groups.empty())
        return false;

    Group& child_group = memo.getGroup(expr.child_groups[0]);
    if (child_group.logical_exprs.empty())
        return false;

    ExprId child_eid = child_group.logical_exprs.back();
    GroupExpr& child_expr = memo.getExpr(child_eid);
    OptNodeType child_type = nodeTypeFromVariantIndex(child_expr.op.index());

    if (!isPenetrable(child_type))
        return false;

    // Don't push Filter through VarLenExpand: the filter may reference variables
    // produced by the VarLenExpand (dst, path, edge), making pushdown incorrect.
    if (child_type == OptNodeType::VarLenExpand)
        return false;

    return true;
}

std::vector<std::unique_ptr<GroupExpr>> FilterPushdownRule::substitute(GroupExpr& expr, Memo& memo) const {
    // Single-step pushdown: Filter(Child(X)) → Child(Filter(X))
    //
    // 1. Move the predicate from the original Filter
    // 2. Move the child operator data from the child GroupExpr
    // 3. Create a new group with a Filter whose child is the child-of-child
    // 4. Create a new GroupExpr for the child operator whose child is the new Filter group
    // 5. Return the child operator GroupExpr (to be inserted into original group)

    // Step 1: Extract predicate from the original Filter
    auto& filter_op = std::get<std::unique_ptr<binder::BoundFilterOp>>(expr.op);
    auto predicate = std::move(filter_op->predicate);

    // Step 2: Get child GroupExpr
    GroupId child_gid = expr.child_groups[0];
    Group& child_group = memo.getGroup(child_gid);
    ExprId child_eid = child_group.logical_exprs.back();
    GroupExpr& child_expr = memo.getExpr(child_eid);

    // Move the child operator data and save its child group reference
    binder::BoundLogicalOperator child_op = std::move(child_expr.op);
    auto grandchild_groups = child_expr.child_groups;

    // Step 3: Create a new group with the pushed-down Filter
    auto new_filter_op = std::make_unique<binder::BoundFilterOp>();
    new_filter_op->predicate = std::move(predicate);

    GroupExpr* filter_gexpr =
        memo.createGroupWithExpr(binder::BoundLogicalOperator(std::move(new_filter_op)), grandchild_groups);

    // Step 4: Create new GroupExpr for the child operator, pointing to new Filter group
    std::vector<GroupId> new_child_groups = {filter_gexpr->group_id};

    auto result =
        std::make_unique<GroupExpr>(memo.newExprId(), expr.group_id, std::move(child_op), std::move(new_child_groups));

    std::vector<std::unique_ptr<GroupExpr>> results;
    results.push_back(std::move(result));
    return results;
}

} // namespace optimizer
} // namespace eugraph
