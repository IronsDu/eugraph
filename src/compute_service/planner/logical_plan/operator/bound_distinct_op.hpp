#pragma once

#include "compute_service/planner/bound_logical_plan_fwd.hpp"

namespace eugraph {
namespace binder {

struct BoundDistinctOp {
    BoundLogicalOperator child;
};

} // namespace binder
} // namespace eugraph
