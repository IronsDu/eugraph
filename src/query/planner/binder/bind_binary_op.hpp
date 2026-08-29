#pragma once

#include "query/parser/ast.hpp"
#include "query/planner/bound_expression/bound_expression.hpp"

namespace eugraph {
namespace binder {

/// Bind-time dispatch for binary operators: resolve the batch kernel by
/// operator and operand semantic types. Pure computation lives in
/// query/evaluator/columnar_kernels.hpp.
BinaryFallbackFn resolveBinaryFallbackFn(cypher::BinaryOperator op, BoundTypeKind left_type, BoundTypeKind right_type);

} // namespace binder
} // namespace eugraph
