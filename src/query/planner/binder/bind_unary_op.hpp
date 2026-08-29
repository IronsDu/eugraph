#pragma once

#include "query/parser/ast.hpp"
#include "query/planner/bound_expression/bound_expression.hpp"

namespace eugraph {
namespace binder {

/// Bind-time dispatch for unary operators: resolve the batch kernel by
/// operator and operand semantic type. Pure computation lives in
/// query/evaluator/columnar_kernels.hpp.
UnaryFallbackFn resolveUnaryFallbackFn(cypher::UnaryOperator op, BoundTypeKind operand_type);

} // namespace binder
} // namespace eugraph
