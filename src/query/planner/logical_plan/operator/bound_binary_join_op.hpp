#pragma once

#include "query/planner/bound_logical_plan_fwd.hpp"
#include "query/planner/logical_plan/operator/join_type.hpp"

namespace eugraph {
namespace binder {

struct BoundBinaryJoinOp {
    JoinType join_type;
    BoundLogicalOperator left;
    BoundLogicalOperator right;
};

} // namespace binder
} // namespace eugraph
