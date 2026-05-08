#pragma once

#include "compute_service/planner/bound_expression/bound_expression_fwd.hpp"
#include "compute_service/planner/bound_logical_plan_fwd.hpp"

namespace eugraph {
namespace binder {

struct BoundFilterOp {
    BoundExpression predicate;
    BoundLogicalOperator child;
};

} // namespace binder
} // namespace eugraph
