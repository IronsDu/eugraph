#pragma once

namespace eugraph {
namespace binder {

/// Produces exactly one empty row. Used as the data source when
/// WITH is the first clause (no preceding MATCH/CREATE).
struct BoundSingletonOp {};

} // namespace binder
} // namespace eugraph
