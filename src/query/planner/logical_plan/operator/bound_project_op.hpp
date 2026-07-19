#pragma once

#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"
#include "query/planner/bound_type.hpp"
#include "query/planner/slot_id.hpp"

#include <string>
#include <vector>

namespace eugraph {
namespace binder {

struct BoundProjectOp {
    struct ProjectItem {
        BoundExpression expr;
        std::string alias;
        BoundType result_type;
        /// SlotId that this item's output column occupies in the Project's
        /// output layout. Filled by the demand-pull lowering pass
        /// (optimizer::lowerAliasPassthrough). INVALID_SLOT_ID until the pass
        /// runs (or for items the pass leaves untouched); the physical planner
        /// falls back to name-based assignment in that case.
        ///
        /// Why this matters: a forwarded graph variable (`WITH n`, `WITH n AS
        /// m`) is rewritten so its expression evaluates the variable's
        /// *object* / property slot, not the raw source slot. The output
        /// column must then be labelled with that derived slot so downstream
        /// references (e.g. covering `m.name`) resolve. Name-based layout
        /// can't know this; the pass does.
        SlotId output_slot = INVALID_SLOT_ID;

        /// SlotId of the source variable at the Project's INPUT scope, when
        /// the item is a bare forward (`WITH n` / `WITH n AS m`). Captured by
        /// `allocateSlotsInOp` at the moment it processes this Project —
        /// BEFORE it allocates fresh alias slots. The captured value reflects
        /// the input scope's name→slot state, which differs from the global
        /// `name_to_slot` state after allocation (outer Projects may have
        /// overwritten the same name). Storing it on the item lets
        /// `collectAliasSlotMapOp` build an acyclic `alias_map` even when
        /// nested Projects reuse the same alias names (§13.12.1 Plan B').
        SlotId input_slot = INVALID_SLOT_ID;

        /// SlotId freshly allocated for this item's alias by
        /// `allocateSlotsInOp`. Distinct from `output_slot` (which may be
        /// redirected to a PEPlan's object slot by `lowerAliasPassthrough`)
        /// and from any other Project's alias of the same name. Used by
        /// `collectAliasSlotMapOp` as the alias_map key.
        SlotId alias_slot = INVALID_SLOT_ID;
    };
    std::vector<ProjectItem> items;
    BoundLogicalOperator child;
};

} // namespace binder
} // namespace eugraph
