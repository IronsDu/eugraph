#pragma once

#include "query/optimizer/analysis/property_demand.hpp"
#include "query/planner/bound_logical_plan.hpp"

#include <unordered_set>

namespace eugraph {
namespace optimizer {

/// Labels that the plan must enforce while scanning/expanding. This is
/// semantic demand, not a physical label-lookup strategy.
struct LabelDemand {
    std::unordered_set<LabelId> required_dst_labels;
};

/// Read-only unified analysis consumed by logical rules and extraction.
/// It carries facts only; no PEPlan/slot layout is produced here.
struct PlanAnalysis {
    PropertyDemand property;
    LabelDemand label;
};

PlanAnalysis analyzePlan(const binder::BoundLogicalOperator& root);

} // namespace optimizer
} // namespace eugraph
