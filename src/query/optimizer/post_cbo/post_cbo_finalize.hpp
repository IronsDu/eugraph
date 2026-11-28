#pragma once

#include "query/planner/bound_logical_plan_fwd.hpp"
#include "query/planner/slot_id.hpp"

#include <string>
#include <unordered_map>

namespace eugraph {
namespace optimizer {

/// Fixed post-CBO rewrite policy, owned by the optimizer.
///
/// The chosen plan is materialized by the physical planner (it owns slot /
/// layout context), so the materialization boundary invokes this single entry
/// point instead of deciding which post-CBO rewrites run. Adding or reordering
/// rewrites here is an optimizer change; the lowering path stays mechanical.
///
/// Today this runs the pattern-comprehension anchor-sharing rewrite. Fully
/// moving the rewrite before CBO requires either PatternComprehensionApply to
/// become a memo/CBO operator or the materialization + slot allocation stage to
/// move into the optimizer (tracked in physical-planner-residue-audit D2).
bool applyPostCboRewrites(binder::BoundLogicalOperator& root, binder::SlotAllocator& slot_alloc,
                          std::unordered_map<std::string, binder::SlotId>& name_to_slot);

} // namespace optimizer
} // namespace eugraph
