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
};

} // namespace binder
} // namespace eugraph
