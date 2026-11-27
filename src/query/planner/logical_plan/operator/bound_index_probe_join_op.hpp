#pragma once

#include "query/planner/bound_logical_plan_fwd.hpp"
#include "query/planner/logical_plan/operator/bound_index_probe_op.hpp"

#include <cstddef>

namespace eugraph {
namespace binder {

/// Binary composition of an anchor subtree and a destination index probe.
///
/// Memo layout (authoritative): exactly two child groups
///   child_groups[kAnchorChild] = anchor, the complete pre-existing left pipeline
///                                (e.g. VLE -> location filters -> DISTINCT);
///   child_groups[kProbeChild]  = probe, a group whose logical expression is a
///                                BoundIndexProbeOp (index range + one edge step).
///
/// The inline fields are NOT the search-space children: `anchor` is the tree
/// transport used by Memo::copyIn/copyOut (the memo stores children as group
/// references), and `probe` mirrors the descriptor so a round-tripped tree can
/// be re-ingested. Rules must never re-select the anchor from `anchor`; they
/// reference the existing group instead.
struct BoundIndexProbeJoinOp {
    static constexpr std::size_t kAnchorChild = 0;
    static constexpr std::size_t kProbeChild = 1;
    static constexpr std::size_t kChildCount = 2;

    BoundLogicalOperator anchor;
    BoundIndexProbeOp probe;
};

} // namespace binder
} // namespace eugraph
