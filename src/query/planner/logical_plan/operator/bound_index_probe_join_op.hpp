#pragma once

#include "query/planner/bound_logical_plan_fwd.hpp"
#include "query/planner/logical_plan/operator/bound_index_probe_op.hpp"

namespace eugraph {
namespace binder {

/// Binary composition of an anchor subtree and a destination index probe.
/// R1 creates the probe; R2 attaches the anchor that already contains the
/// complete friend/location/DISTINCT pipeline. This operator deliberately keeps
/// both as child groups so no rule has to re-select the anchor.
struct BoundIndexProbeJoinOp {
    BoundLogicalOperator anchor;
    BoundIndexProbeOp probe;
};

} // namespace binder
} // namespace eugraph
