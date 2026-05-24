#pragma once

#include "query/planner/bound_logical_plan_fwd.hpp"

#include <cstdint>
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
    /// Pairs of (left_column_index, right_column_index) for correlated variables.
    std::vector<std::pair<uint32_t, uint32_t>> correlation;
};

} // namespace binder
} // namespace eugraph
