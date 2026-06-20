#pragma once

#include "query/optimizer/materialization_req.hpp"
#include "query/planner/bound_expression/bound_expression.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"

namespace eugraph {
namespace catalog {
class Catalog;
}

namespace optimizer {

// ============================================================
// RequirementCollector — pre-Cascades pass that walks the bound
// logical tree and produces the per-variable materialization
// requirements (VarRequirements) for every logical operator node.
//
// These requirements are stored alongside each Memo group and drive
// Enricher enforcer placement during Cascades optimisation:
//   - Strong-mode access (BoundPropertyRef with candidates) becomes
//     need_props[label_id] = {prop_id, ...}.
//   - Weak-mode access (BoundDynamicPropertyRef, labels(n), label-cast)
//     becomes need_labels = true for that variable.
//     If `catalog` is provided, also resolves the property name across
//     all labels to populate need_props (triggering the lighter
//     PropertyExtract path instead of full VertexValue materialisation).
//
// The collector is a pure read pass: it does not modify the input
// plan.
// ============================================================

/// Walks a single BoundExpression, appending materialization requirements
/// for any BoundPropertyRef / BoundDynamicPropertyRef / label function
/// call found in `dst`. `dst` is merged across all expressions visited
/// in the same operator.
void collectExprRequirements(const binder::BoundExpression& expr, VarRequirements& dst,
                             const catalog::Catalog* catalog = nullptr);

/// Walks a single BoundLogicalOperator and its children, producing the
/// merged VarRequirements for the entire subtree rooted at `op`.
VarRequirements collectOpRequirements(const binder::BoundLogicalOperator& op,
                                      const catalog::Catalog* catalog = nullptr);

} // namespace optimizer
} // namespace eugraph
