#pragma once

// Content-aware equality for BoundLogicalOperator and BoundExpression.
//
// Pairs with operator_hash.cpp — the hash narrows the search to a bucket,
// and these visitors confirm structural equality before the Memo merges two
// GroupExpr into one group. Like the hash, we deliberately do NOT compare
// child/left/right fields — the Memo canonicalizes children via child_groups.

#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"

namespace eugraph {
namespace optimizer {

bool equalBoundLogicalOperator(const binder::BoundLogicalOperator& a, const binder::BoundLogicalOperator& b);
bool equalBoundExpression(const binder::BoundExpression& a, const binder::BoundExpression& b);

} // namespace optimizer
} // namespace eugraph
