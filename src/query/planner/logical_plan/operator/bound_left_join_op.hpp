#pragma once

#include "query/planner/bound_logical_plan_fwd.hpp"

#include <utility>
#include <vector>

namespace eugraph {
namespace binder {

/// Left join: preserves all rows from the left child. For each left row, if the
/// right sub-plan (fed via CorrelatedSource) produces matches, emit the
/// combined rows. If no match, emit the left row with NULLs for all right
/// columns.
struct BoundLeftJoinOp {
    BoundLogicalOperator left;
    BoundLogicalOperator right;
    /// Pairs of (left_column_index, right_column_index) for correlated
    /// variables. Column indices are used here (rather than SlotIds as in
    /// BoundSemiJoinOp) because OPTIONAL MATCH's left plan output preserves
    /// the binder's column_index ordering — ProjectionExtract only appends
    /// columns, never reorders. SlotId-based lookup fails when the left plan
    /// contains a WITH projection that forwards a graph variable: the Project
    /// emits under the PEPlan object_slot, while the binder still holds the
    /// topology slot_id, so the lookup misses (Match7 [4][6][10]...).
    std::vector<std::pair<uint32_t, uint32_t>> correlation;
};

} // namespace binder
} // namespace eugraph
