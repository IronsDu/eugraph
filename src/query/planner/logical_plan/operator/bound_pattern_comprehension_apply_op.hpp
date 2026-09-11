#pragma once

#include "query/planner/bound_logical_plan_fwd.hpp"
#include "query/planner/bound_type.hpp"
#include "query/planner/slot_id.hpp"

#include <string>
#include <utility>
#include <vector>

namespace eugraph {
namespace binder {

/// PatternComprehensionApply: per-row correlated sub-plan executor that
/// produces one or more list-typed columns collecting the projection results
/// of a Cypher PatternComprehension (`[(n)-->(m) | m]`, etc.).
///
/// Structure mirrors BoundSemiJoinOp: `left` is the outer plan; `right` is a
/// correlated sub-plan whose leaf is BoundCorrelatedSourceOp. Each outer row
/// triggers one full execution of `right` with correlation values injected;
/// all resulting rows' first column(s) are collected into ListValue(s) and
/// appended as new columns on the output (left columns + one list column per
/// entry in output_slots).
struct BoundPatternComprehensionApplyOp {
    BoundLogicalOperator left;
    BoundLogicalOperator right;
    struct Correlation {
        SlotId left_slot = INVALID_SLOT_ID;
        uint32_t left_column = 0;
        std::string left_var;
        SlotId right_slot = INVALID_SLOT_ID;
        bool operator==(const Correlation& o) const {
            return left_slot == o.left_slot && left_column == o.left_column && left_var == o.left_var &&
                   right_slot == o.right_slot;
        }
    };
    std::vector<Correlation> correlation;

    /// One entry per PatternComprehension sharing this Apply op. Order
    /// matches the right sub-plan's aggregate output order.
    struct Output {
        SlotId slot_id = INVALID_SLOT_ID;
        std::string name; // e.g. "__pc_1"
        BoundType element_type = BoundType::Any();
    };
    std::vector<Output> outputs;

    /// True when every consumer of this Apply only asks "does at least one
    /// match exist?" — i.e. the comprehension came from a boolean-context
    /// pattern predicate (`NOT (a)-[:R]-(b)`, `EXISTS { ... }`, `WHERE (a)-->(b)`)
    /// and is consumed as `size(<list>) > 0`. The collected list's *contents*
    /// are then dead, so the physical operator may stop draining the correlated
    /// sub-plan at the first matching row and replace the real elements with a
    /// single placeholder element. Only emptiness is observable, so the
    /// downstream `size(list) > 0` yields exactly the same truth value
    /// (0 elements -> false, 1 placeholder -> true).
    ///
    /// Set only for existence-derived comprehensions; a user-visible
    /// comprehension (`[(a)-->(b) | b]` used as a list) always stays false.
    bool existence_only = false;
};

} // namespace binder
} // namespace eugraph
