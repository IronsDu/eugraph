#pragma once

#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/bound_type.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace eugraph {
namespace binder {

/// ALL(x IN list WHERE pred) — returns true when every element satisfies pred.
/// An empty list yields true.
///
/// loop_column_index: the column index assigned to the loop variable during
/// Binder scoping. At evaluation time, a temporary DataChunk is built with
/// the loop variable at this index, allowing BoundColumnRef in the where_pred
/// to resolve through the normal evaluateInternal path.
struct BoundAllExpr {
    std::string variable;                      // loop variable name (e.g. "x")
    uint32_t loop_column_index = 0;            // column index for the loop variable
    BoundExpression list_expr;                 // expression producing List<T>
    std::optional<BoundExpression> where_pred; // optional filter predicate
    BoundType result_type;                     // always Bool()
};

/// ANY(x IN list WHERE pred) — returns true when at least one element satisfies pred.
/// An empty list yields false.
struct BoundAnyExpr {
    std::string variable;
    uint32_t loop_column_index = 0;
    BoundExpression list_expr;
    std::optional<BoundExpression> where_pred;
    BoundType result_type;
};

/// NONE(x IN list WHERE pred) — returns true when no element satisfies pred.
/// An empty list yields true. Equivalent to NOT ANY(...).
struct BoundNoneExpr {
    std::string variable;
    uint32_t loop_column_index = 0;
    BoundExpression list_expr;
    std::optional<BoundExpression> where_pred;
    BoundType result_type;
};

/// SINGLE(x IN list WHERE pred) — returns true when exactly one element satisfies pred.
/// An empty list yields false.
struct BoundSingleExpr {
    std::string variable;
    uint32_t loop_column_index = 0;
    BoundExpression list_expr;
    std::optional<BoundExpression> where_pred;
    BoundType result_type;
};

/// [x IN list | projection] or [x IN list WHERE pred | projection]
/// Evaluates the projection for each element of the list and collects results.
struct BoundListComprehension {
    std::string variable;
    uint32_t loop_column_index = 0;
    BoundExpression list_expr;
    std::optional<BoundExpression> where_pred;
    BoundExpression projection;
    BoundType result_type; // List<T> where T is the projection's result type
};

} // namespace binder
} // namespace eugraph
