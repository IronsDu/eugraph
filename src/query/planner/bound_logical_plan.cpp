#include "query/planner/bound_logical_plan.hpp"

#include "query/optimizer/framework/chosen_plan.hpp"

namespace eugraph {
namespace binder {

BoundLogicalPlan::BoundLogicalPlan() = default;
BoundLogicalPlan::~BoundLogicalPlan() = default;
BoundLogicalPlan::BoundLogicalPlan(BoundLogicalPlan&&) = default;
BoundLogicalPlan& BoundLogicalPlan::operator=(BoundLogicalPlan&&) = default;

} // namespace binder
} // namespace eugraph
