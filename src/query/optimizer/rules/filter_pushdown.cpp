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
    case OptNodeType::Sort:
    case OptNodeType::Skip:
    case OptNodeType::Limit:
    case OptNodeType::Distinct:
        return true;
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

    // Pushing a Filter through an operator that introduces new variables is
    // only safe if the predicate does not reference any of those variables.
    // Otherwise the pushed Filter would be placed where those variables are
    // not yet in scope, leaving dangling column references.
    const auto& filter_op = std::get<std::unique_ptr<binder::BoundFilterOp>>(expr.op);
    auto introduced = introducedVariableNames(child_expr.op);
    if (!introduced.empty()) {
        std::unordered_set<std::string> referenced;
        collectColumnNames(filter_op->predicate, referenced);
        for (const auto& name : introduced) {
            if (referenced.count(name))
                return false;
        }
    }

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
