#pragma once

#include "query/planner/bound_logical_plan_fwd.hpp"

#include <cstdint>
#include <vector>

namespace eugraph {
namespace binder {

/// SemiJoin: left semi-join. Emits rows from left that have at least one
/// matching row in right, based on correlated column values injected from
/// left into the right sub-plan via a CorrelatedSource leaf node.
struct BoundSemiJoinOp {
    BoundLogicalOperator left;
    BoundLogicalOperator right;
    /// Pairs of (left_column_index, right_column_index) for correlated variables.
    std::vector<std::pair<uint32_t, uint32_t>> correlation;
    /// If true, emit rows where the right sub-plan produces NO match (anti-semi-join).
    bool anti = false;
};

} // namespace binder
} // namespace eugraph
