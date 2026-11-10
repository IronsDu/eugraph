#pragma once

#include "query/planner/bound_logical_plan_fwd.hpp"
#include "query/planner/bound_type.hpp"
#include "query/planner/logical_plan/operator/bound_pattern_comprehension_apply_op.hpp"
#include "query/planner/slot_id.hpp"

#include <string>
#include <utility>
#include <vector>

namespace eugraph {
namespace binder {

/// PatternComprehensionFork: one shared correlated anchor feeding several
/// pattern-comprehension branches.
///
/// This is the plan-level counterpart of two `PatternComprehensionApplyOp`s
/// whose right sub-plans share a pure, correlation-identical prefix.  The two
/// comprehensions of LDBC complex-14 (`a <-[:HAS_CREATOR]- comment
/// -[:REPLY_OF]-> Post/Comment -[:HAS_CREATOR]-> b`) differ only in the label
/// of the `REPLY_OF` target, so the `a <-[:HAS_CREATOR]- comment` expansion —
/// 4.1M scanned rows at SF1, 97.5% of the query's scanRows — is written down
/// once instead of once per comprehension.
///
/// Execution model (see `PatternComprehensionForkPhysicalOp`):
///   * `left` produces the outer rows (one row per correlation unit);
///   * `anchor` is executed once per outer row / batch and its output is
///     materialised into a spool;
///   * every `branches[i]` starts from `BoundCorrelatedSourceOp` and consumes
///     the spooled anchor rows, then aggregates them into one list column;
///   * the fork appends one list column per branch to the `left` row.
///
/// The anchor rows are an *explicit plan edge*, not an operator-local cache:
/// the anchor is a child of the fork and is planned/executed once.
///
/// Child order in the memo / extraction:
///   child_groups[0] = left, child_groups[1] = anchor,
///   child_groups[2 + i] = branches[i].
struct BoundPatternComprehensionForkOp {
    BoundLogicalOperator left;
    BoundLogicalOperator anchor;
    std::vector<BoundLogicalOperator> branches;

    /// Columns of `left` fed into the anchor's `BoundCorrelatedSourceOp`.
    std::vector<BoundPatternComprehensionApplyOp::Correlation> correlation;

    /// One entry per branch, in branch order. The list column is appended to
    /// the output after the left columns.
    std::vector<BoundPatternComprehensionApplyOp::Output> outputs;

    /// Batch key injected into the right sub-plans (mirrors the Apply op).
    bool batch_enabled = false;
    SlotId batch_slot_id = INVALID_SLOT_ID;
    std::string batch_var;

    size_t branchCount() const {
        return branches.size();
    }
    /// left + anchor + one entry per branch.
    static size_t childCountFor(size_t branch_count) {
        return 2 + branch_count;
    }
};

} // namespace binder
} // namespace eugraph
