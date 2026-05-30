#pragma once

#include "query/planner/bound_logical_plan_fwd.hpp"

namespace eugraph {
namespace binder {

struct BoundUnionOp {
    bool all; // true = UNION ALL, false = UNION (dedup)
    BoundLogicalOperator left;
    BoundLogicalOperator right;
};

} // namespace binder
} // namespace eugraph
