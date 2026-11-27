#pragma once

#include "query/planner/bound_logical_plan.hpp"
#include "query/planner/slot_semantics.hpp"

namespace eugraph {
namespace catalog {
class Catalog;
}
namespace optimizer {

/// Annotate direct Filter(LabelScan) with an optimizer-selected index path and
/// replace the Filter when every predicate is consumed. Shared by the
/// pre-memo normalization pass and the Cascades access-path rule.
void applyIndexAccessPaths(binder::BoundLogicalOperator& op, const catalog::Catalog& catalog,
                           const binder::SlotSemanticMap* semantics);

} // namespace optimizer
} // namespace eugraph
