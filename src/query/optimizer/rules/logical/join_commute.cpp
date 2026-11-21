#include "query/optimizer/rules/logical/join_commute.hpp"

#include "query/optimizer/analysis/logical_plan_walk.hpp"
#include "query/optimizer/framework/memo.hpp"
#include "query/planner/logical_plan/operator/bound_binary_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_create_edge_op.hpp"
#include "query/planner/logical_plan/operator/bound_create_node_op.hpp"
#include "query/planner/logical_plan/operator/bound_delete_op.hpp"
#include "query/planner/logical_plan/operator/bound_foreach_op.hpp"
#include "query/planner/logical_plan/operator/bound_merge_op.hpp"
#include "query/planner/logical_plan/operator/bound_pattern_comprehension_apply_op.hpp"
#include "query/planner/logical_plan/operator/bound_pattern_comprehension_fork_op.hpp"
#include "query/planner/logical_plan/operator/bound_remove_op.hpp"
#include "query/planner/logical_plan/operator/bound_set_op.hpp"

#include <type_traits>

namespace eugraph {
namespace optimizer {
namespace {

bool unsafeForCommute(const binder::BoundLogicalOperator& op) {
    bool unsafe = false;
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundCreateEdgeOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundSetOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundRemoveOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundDeleteOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundMergeOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundForeachOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundPatternComprehensionApplyOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundPatternComprehensionForkOp>>) {
                unsafe = true;
            }
        },
        op);
    if (unsafe)
        return true;
    forEachLogicalChild(op, [&](const binder::BoundLogicalOperator& child) {
        if (!unsafe)
            unsafe = unsafeForCommute(child);
    });
    return unsafe;
}

} // namespace

PatternNode JoinCommuteRule::pattern() const {
    return {OptNodeType::BinaryJoin, {{}, {}}};
}

bool JoinCommuteRule::condition(GroupExpr& expr, Memo& memo) const {
    if (!memo.commuteSafe())
        return false;
    auto* join = std::get_if<std::unique_ptr<binder::BoundBinaryJoinOp>>(&expr.op);
    if (!join || !*join)
        return false;
    if ((*join)->join_type != binder::JoinType::Cross && (*join)->join_type != binder::JoinType::Inner)
        return false;
    return !unsafeForCommute((*join)->left) && !unsafeForCommute((*join)->right);
}

std::vector<std::unique_ptr<GroupExpr>> JoinCommuteRule::substitute(GroupExpr& expr, Memo& /*memo*/) const {
    auto* join = std::get_if<std::unique_ptr<binder::BoundBinaryJoinOp>>(&expr.op);
    if (!join || !*join || expr.child_groups.size() != 2)
        return {};

    auto swapped = std::make_unique<binder::BoundBinaryJoinOp>();
    swapped->join_type = (*join)->join_type;
    swapped->join_conditions.reserve((*join)->join_conditions.size());
    for (const auto& cond : (*join)->join_conditions)
        swapped->join_conditions.push_back(cloneBoundExpression(cond));
    swapped->left = cloneBoundLogicalOperator((*join)->right);
    swapped->right = cloneBoundLogicalOperator((*join)->left);

    std::vector<GroupId> children = expr.child_groups;
    std::swap(children[0], children[1]);
    std::vector<std::unique_ptr<GroupExpr>> out;
    out.push_back(std::make_unique<GroupExpr>(INVALID_EXPR_ID, expr.group_id,
                                              binder::BoundLogicalOperator(std::move(swapped)), std::move(children)));
    return out;
}

} // namespace optimizer
} // namespace eugraph
