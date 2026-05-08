#pragma once

#include "compute_service/planner/bound_logical_plan_fwd.hpp"

#include <cstdint>

namespace eugraph {
namespace binder {

struct BoundSkipOp {
    int64_t count;
    BoundLogicalOperator child;
};

} // namespace binder
} // namespace eugraph
