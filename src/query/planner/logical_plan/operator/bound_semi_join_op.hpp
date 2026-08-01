#pragma once

#include "query/planner/bound_logical_plan_fwd.hpp"
#include "query/planner/slot_id.hpp"

#include <vector>

namespace eugraph {
namespace binder {

/// SemiJoin: left semi-join. Emits rows from left that have at least one
/// matching row in right, based on correlated column values injected from
/// left into the right sub-plan via a CorrelatedSource leaf node.
struct BoundSemiJoinOp {
    BoundLogicalOperator left;
    BoundLogicalOperator right;
    /// Pairs of (left_slot_id, right_slot_id) for correlated variables. SlotIds
    /// are used (rather than binder column_index) because ProjectionExtract
    /// appends columns to the physical layout, making binder column_index
    /// values no longer match physical positions. The physical planner resolves
    /// each slot_id to its column position via the child's TupleSlotLayout.
    std::vector<std::pair<SlotId, SlotId>> correlation;
    /// If true, emit rows where the right sub-plan produces NO match (anti-semi-join).
    bool anti = false;
};

} // namespace binder
} // namespace eugraph
