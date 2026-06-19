#pragma once

#include "query/optimizer/cost_model.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"

#include <cstdint>
#include <memory>

namespace eugraph {
namespace optimizer {

// ============================================================
// PhysicalExpr — physical operator descriptor in the Memo
//
// A lightweight sibling to BoundLogicalOperator. Carries the
// PhysicalOpTag (which physical operator to use) and a clone of the
// source logical operator (for data: label_ids, predicates, etc.).
//
// Implementation rules (rules/impl/*.cpp) produce GroupExprs whose
// phys_op_ field holds a PhysicalExpr. At copyOut time, the winner's
// PhysicalExpr tree is extracted as a ChosenPlan.
// ============================================================
struct PhysicalExpr {
    PhysicalOpTag tag = PhysicalOpTag::NodeScan;
    binder::BoundLogicalOperator source;
};

uint64_t hashPhysicalExpr(const PhysicalExpr& phys);
bool equalPhysicalExpr(const PhysicalExpr& a, const PhysicalExpr& b);
PhysicalExpr clonePhysicalExpr(const PhysicalExpr& src);

} // namespace optimizer
} // namespace eugraph
