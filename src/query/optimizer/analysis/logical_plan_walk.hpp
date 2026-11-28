#pragma once

#include "query/planner/bound_logical_plan.hpp"

#include "query/planner/logical_plan/operator/bound_index_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_index_probe_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_index_probe_op.hpp"
#include "query/planner/logical_plan/operator/bound_pattern_comprehension_fork_op.hpp"
#include <type_traits>
#include <utility>

namespace eugraph {
namespace optimizer {

/// Visit every direct child of a logical operator. Kept in one place so
/// optimizer passes do not grow a private copy of the variant switch.
template <typename OpRef, typename Visit> void forEachLogicalChild(OpRef&& op, Visit&& visit) {
    std::visit(
        [&](auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundAggregateOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundUnwindOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundPathBuildOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundPathCountOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundSkipOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundLimitOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundDistinctOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundSetOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundRemoveOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundDeleteOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundMergeOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundCreateEdgeOp>>) {
                if (v)
                    visit(v->child);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundIndexJoinOp>>) {
                if (v)
                    visit(v->left);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundIndexProbeJoinOp>>) {
                // Only the anchor is a logical subtree; the probe is a leaf
                // descriptor carried by its own memo group.
                if (v)
                    visit(v->anchor);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundIndexProbeOp>>) {
                // Probe descriptor: no logical children.
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>>) {
                if (v && v->child)
                    visit(*v->child);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundForeachOp>>) {
                if (v) {
                    visit(v->child);
                    visit(v->body);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryJoinOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundLeftJoinOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSemiJoinOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundPatternComprehensionApplyOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundUnionOp>>) {
                if (v) {
                    visit(v->left);
                    visit(v->right);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPatternComprehensionForkOp>>) {
                // Shared-anchor fork: left, anchor, then one sub-plan per branch.
                if (v) {
                    visit(v->left);
                    visit(v->anchor);
                    for (auto& branch : v->branches)
                        visit(branch);
                }
            }
        },
        op);
}

/// Visit every node of a bound logical plan (pre-order), including the root.
template <typename Visit> void forEachPlanNode(const binder::BoundLogicalOperator& op, Visit&& visit) {
    visit(op);
    forEachLogicalChild(const_cast<binder::BoundLogicalOperator&>(op),
                        [&](binder::BoundLogicalOperator& child) { forEachPlanNode(child, visit); });
}

} // namespace optimizer
} // namespace eugraph
