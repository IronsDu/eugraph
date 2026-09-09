#pragma once

#include "query/planner/bound_logical_plan_fwd.hpp"
#include "query/planner/logical_plan/operator/join_type.hpp"
#include "query/planner/slot_id.hpp"

namespace eugraph {
namespace binder {

struct BoundBinaryJoinOp {
    JoinType join_type;
    BoundLogicalOperator left;
    BoundLogicalOperator right;
    // When true, plan as ApplyPhysicalOp: each left row is injected into the
    // right subtree through a BoundCorrelatedSourceOp leaf.
    bool correlated = false;
    std::vector<std::pair<SlotId, SlotId>> correlation;
    // Hash join keys are physical column indices local to each child.
    std::vector<uint32_t> left_keys;
    std::vector<uint32_t> right_keys;
};

} // namespace binder
} // namespace eugraph
