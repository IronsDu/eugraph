#pragma once

#include "query/planner/bound_logical_plan_fwd.hpp"
#include "query/planner/slot_id.hpp"

#include <string>
#include <vector>

namespace eugraph {
namespace binder {

/// Left join: preserves all rows from the left child. For each left row, if the
/// right sub-plan (fed via CorrelatedSource) produces matches, emit the
/// combined rows. If no match, emit the left row with NULLs for all right
/// columns.
struct BoundLeftJoinOp {
    struct Correlation {
        /// SlotId of the outer variable in the left plan. The physical planner
        /// prefers resolving through the left TupleSlotLayout.
        SlotId left_slot = INVALID_SLOT_ID;
        /// Binder column index fallback (used when a WITH projection forwards
        /// a graph variable under a different slot than the binder recorded).
        uint32_t left_column = 0;
        /// Variable name fallback (used when slot/column both miss).
        std::string left_var;
        /// Right-side local column index consumed by CorrelatedSource.
        uint32_t right_column = 0;

        bool operator==(const Correlation& other) const {
            return left_slot == other.left_slot && left_column == other.left_column && left_var == other.left_var &&
                   right_column == other.right_column;
        }
    };
    BoundLogicalOperator left;
    BoundLogicalOperator right;
    std::vector<Correlation> correlation;
};

} // namespace binder
} // namespace eugraph
