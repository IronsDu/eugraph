#pragma once

#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/bound_type.hpp"

#include <cstdint>
#include <string>

namespace eugraph {
namespace binder {

/// reduce(acc = init, x IN list | expr)
/// `acc_column_index` / `loop_column_index` are temporary columns appended
/// after the input columns; the evaluator builds a one-row chunk with those
/// cells for each list element.
struct BoundReduceExpr {
    std::string accumulator;
    uint32_t accumulator_column_index = 0;
    std::string variable;
    uint32_t loop_column_index = 0;
    BoundExpression init;
    BoundExpression list_expr;
    BoundExpression expr;
    BoundType result_type;
};

} // namespace binder
} // namespace eugraph
