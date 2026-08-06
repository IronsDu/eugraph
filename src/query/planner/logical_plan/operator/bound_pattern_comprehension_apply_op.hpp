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
    /// Pairs of (left_slot_id, right_slot_id) for correlated variables.
    /// Same semantics as BoundSemiJoinOp::correlation.
    std::vector<std::pair<SlotId, SlotId>> correlation;

    /// One entry per PatternComprehension sharing this Apply op. Order
    /// matches the right sub-plan's aggregate output order.
    struct Output {
        SlotId slot_id = INVALID_SLOT_ID;
        std::string name; // e.g. "__pc_1"
        BoundType element_type = BoundType::Any();
    };
    std::vector<Output> outputs;
};

} // namespace binder
} // namespace eugraph
