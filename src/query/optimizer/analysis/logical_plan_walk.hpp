#pragma once

#include "query/planner/bound_logical_plan.hpp"

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

/// Optimizer pass: select the bounded multi-target shortestPath strategy. The
/// physical operator only executes it when this bit is set; it must not infer
/// the strategy from runtime batch shape.
inline void markShortestPathBatch(binder::BoundLogicalOperator& op) {
    std::visit(
        [](auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                if (v && v->shortest_path && !v->all_shortest_paths && v->max_hops >= 0 && v->max_hops <= 8)
                    v->batch_shortest_path = true;
            }
        },
        op);
    forEachLogicalChild(op, [](binder::BoundLogicalOperator& child) { markShortestPathBatch(child); });
}

} // namespace optimizer
} // namespace eugraph
