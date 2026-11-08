#pragma once

#include "query/planner/bound_logical_plan.hpp"

#include <unordered_set>

namespace eugraph {
namespace optimizer {

/// Read-only property-demand classification for the late-materialization
/// boundary. It does not allocate slots or build physical operators.
struct PropertyDemand {
    bool has_limit = false;
    /// Slots referenced by the subtree below the final Project (Filter, Sort,
    /// Aggregate, joins, ...).
    std::unordered_set<binder::SlotId> required_below_limit;
    /// Final Project slots not referenced below: candidates for materializing
    /// after LIMIT.
    std::unordered_set<binder::SlotId> output_only;
};

PropertyDemand analyzePropertyDemand(const binder::BoundLogicalOperator& root);
PropertyDemand analyzePropertyDemand(const binder::BoundProjectOp& project, const binder::BoundLogicalOperator& child);

} // namespace optimizer
} // namespace eugraph
