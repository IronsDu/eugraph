#pragma once

#include "query/optimizer/cost_model.hpp"
#include "query/optimizer/materialization_req.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace eugraph {
namespace optimizer {

// ============================================================
// PhysicalExpr — physical operator descriptor in the Memo
//
// A lightweight sibling to BoundLogicalOperator. Carries the
// PhysicalOpTag (which physical operator to use) and a clone of the
// source logical operator (for data: label_ids, predicates, etc.).
//
// Fields specific to the topology/semantic split:
//   enrich_variable     — for Enricher ops, the variable being upgraded
//                         (e.g. VertexEnrich over variable "n"). Empty for
//                         all other tags.
//   output_mat          — materializations this operator PROVIDES in its
//                         output PhysProp (Enrichers declare theirs here).
//                         Used by O_INPUTS to assemble each input's
//                         provided PhysProp.
//   required_input_mat  — per-input materializations this operator
//                         REQUIRES from its child subplans. Each entry
//                         is indexed by child position (0, 1, ...) and
//                         compared against each child winner's output.
//                         Example: a Filter referencing n.name demands
//                         need_props[Person]={name} from its single input.
//
// Implementation rules (rules/impl/*.cpp) produce GroupExprs whose
// phys_op_ field holds a PhysicalExpr. At copyOut time, the winner's
// PhysicalExpr tree is extracted as a ChosenPlan.
// ============================================================
struct PhysicalExpr {
    PhysicalOpTag tag = PhysicalOpTag::NodeScan;
    binder::BoundLogicalOperator source;

    // Enricher-only: target variable name.
    std::string enrich_variable;

    // Output materializations (what this op provides).
    VarRequirements output_mat;

    // Per-input required materializations. Index = child position.
    // Empty for leaf operators (no inputs).
    std::vector<VarRequirements> required_input_mat;
};

uint64_t hashPhysicalExpr(const PhysicalExpr& phys);
bool equalPhysicalExpr(const PhysicalExpr& a, const PhysicalExpr& b);
PhysicalExpr clonePhysicalExpr(const PhysicalExpr& src);

} // namespace optimizer
} // namespace eugraph
