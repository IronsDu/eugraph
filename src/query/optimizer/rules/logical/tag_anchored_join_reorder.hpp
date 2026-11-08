#pragma once

#include "common/types/graph_types.hpp"
#include "query/planner/bound_logical_plan.hpp"

#include <unordered_map>

namespace eugraph {
namespace catalog {
class Catalog;
}
namespace optimizer {

/// Reorder the LDBC c6 tag-co-occurrence pattern so the selective knownTag
/// anchor drives the pattern:
///
///   friends UNWIND -> f -HAS_CREATOR-> post -HAS_TAG-> t(knownTag)
///                  -> post -HAS_TAG-> tag
///
/// becomes
///
///   knownTag -HAS_TAG-> post -HAS_CREATOR-> f
///     CROSS friends(collect(distinct friend))
///     FILTER f IN friends
///     post -HAS_TAG-> tag
///
/// The guard requires a selective Tag label relative to Post and a shape with
/// a one-row friends aggregate; otherwise the plan is left untouched.
/// Returns true when the plan was rewritten.
bool rewriteTagAnchoredJoin(binder::BoundLogicalPlan& plan, const catalog::Catalog* catalog,
                            const std::unordered_map<LabelId, uint64_t>* label_vertex_counts = nullptr);

} // namespace optimizer
} // namespace eugraph
