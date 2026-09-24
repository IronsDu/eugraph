#pragma once

#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"
#include "query/planner/bound_type.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace eugraph {
namespace binder {

/// FOREACH (variable IN list_expr | body...)
///
/// `body` is a chain of *updating* operators whose leaf is a
/// BoundCorrelatedSourceOp. For every input row the executor runs that chain once
/// per list element with the correlated values injected, then passes the input row
/// through unchanged, so cardinality is preserved (unlike UNWIND, which
/// multiplies rows).
///
/// Scoping follows neo4j: `variable` lives in a sub-scope, so it shadows an outer
/// variable of the same name inside the body and is invisible after the clause;
/// variables the body creates are equally local to it. The correlated source
/// carries every outer variable the body can see (minus a shadowed one) plus the
/// element as its last column -- see `input_columns` / `element_column` for how the
/// executor feeds it from the input row.
struct BoundForeachOp {
    BoundExpression list_expr;
    std::string variable;
    BoundType element_type;
    /// Sub-plan column that receives the current element; always the last
    /// correlated column, i.e. `input_columns.size()`.
    uint32_t element_column = 0;
    /// Sub-plan column i is fed from column `input_columns[i]` of the input row.
    /// An empty vector means the body never reads an outer variable.
    std::vector<uint32_t> input_columns;
    /// Body chain, rooted at a BoundCorrelatedSourceOp.
    BoundLogicalOperator body;
    BoundLogicalOperator child;
};

} // namespace binder
} // namespace eugraph
