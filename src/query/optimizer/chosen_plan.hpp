#pragma once

#include "query/optimizer/cost_model.hpp"
// Full bound_logical_plan.hpp needed because ChosenPlan holds BoundLogicalOperator
// by value, which requires complete operator alternative types.
#include "query/planner/bound_logical_plan.hpp"

#include <memory>
#include <vector>

namespace eugraph {
namespace optimizer {

// ============================================================
// ChosenPlan — the CBO's chosen physical plan tree
//
// Produced by Memo::copyOut when the winner is a physical GroupExpr.
// Each node carries the PhysicalOpTag and the source logical operator
// data (cloned). Consumed by PhysicalPlanner::planChosen to build
// the executable PhysicalOperator tree.
//
// Null plan.chosen means CBO didn't produce a physical winner (e.g.
// no implementation rules fired for some group); fall back to
// plan.root → planBound path.
// ============================================================
struct ChosenPlan {
    PhysicalOpTag tag = PhysicalOpTag::NodeScan;
    binder::BoundLogicalOperator op;
    std::vector<std::unique_ptr<ChosenPlan>> children;
};

} // namespace optimizer
} // namespace eugraph
