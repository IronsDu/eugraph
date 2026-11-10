#pragma once

#include "query/planner/bound_logical_plan_fwd.hpp"
#include "query/planner/slot_id.hpp"

#include <string>
#include <unordered_map>

namespace eugraph {
namespace optimizer {

/// Share the anchor expansion of two sibling pattern comprehensions.
///
/// LDBC complex-14 computes two weights over the same prefix:
///
///   (a:Person)<-[:HAS_CREATOR]-(:Comment)-[:REPLY_OF]->(:Post)-[:HAS_CREATOR]->(b)
///   (a:Person)<-[:HAS_CREATOR]-(:Comment)-[:REPLY_OF]->(:Comment)-[:HAS_CREATOR]->(b)
///
/// The binder hoists each comprehension as its own `PatternComprehensionApplyOp`,
/// so the `a <-[:HAS_CREATOR]- comment` expansion (4.1M scanned rows at SF1, 97.5%
/// of the query's scanRows) runs once per comprehension. The two right sub-plans
/// differ only in the `REPLY_OF` destination label, so everything from the anchor
/// expansion down is identical up to generated variable names.
///
/// This pass recognises that shape and rewrites the two stacked applies into one
/// apply whose right sub-plan starts with `BoundPatternComprehensionForkOp`:
/// the anchor is a single child of the fork and is executed once per outer batch,
/// both branch tails consume the materialised anchor rows.
///
/// Guards (fail closed — the original plan is kept):
///   * both applies must be plain list-producing comprehensions (no existence
///     shortcut, same non-empty correlation, same batch key);
///   * the inner applies must have the same correlation and an identical left
///     sub-plan;
///   * the shared suffix must be *structurally identical* after renaming the
///     generated variables of the second comprehension onto the first;
///   * the shared suffix must contain at least one pattern operator (Expand /
///     Scan / join), so a fork never replaces a trivial pipeline.
///
/// Returns true when the plan was rewritten.
bool rewritePatternComprehensionAnchorShare(binder::BoundLogicalOperator& root, binder::SlotAllocator& slot_alloc,
                                            std::unordered_map<std::string, binder::SlotId>& name_to_slot);

} // namespace optimizer
} // namespace eugraph
