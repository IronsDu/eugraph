#pragma once

// Content-aware hashing for BoundLogicalOperator and BoundExpression.
//
// The Memo's dedup (Columbia: SSP::FindDup) compares operator content, not
// just variant type. Without content-aware hashing, two distinct operators of
// the same type (e.g. two BoundLabelScanOp with different label_ids) collide
// and get incorrectly merged into one group.
//
// These visitors mirror `cloneBoundLogicalOperator` / `cloneBoundExpression`
// in memo.cpp field-for-field, so any new variant alternative must be added
// here as well. We deliberately do NOT hash child/left/right fields — the
// Memo canonicalizes children via child_groups, and including the child tree
// would re-walk the entire plan on every hash call.

#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"

#include <cstdint>

namespace eugraph {
namespace optimizer {

uint64_t hashBoundLogicalOperator(const binder::BoundLogicalOperator& op);
uint64_t hashBoundExpression(const binder::BoundExpression& expr);

} // namespace optimizer
} // namespace eugraph
