#pragma once

#include "query/dataset/data_chunk.hpp"
#include "query/parser/ast.hpp"
#include "query/planner/bound_expression/bound_expression.hpp"

#include <functional>

namespace eugraph {
namespace function {

/// Resolve the batch function for a BoundBinaryOp based on operator and operand types.
binder::BinaryBatchFn resolveBinaryBatchFn(cypher::BinaryOperator op, binder::BoundTypeKind left_type,
                                           binder::BoundTypeKind right_type);

/// Resolve the batch function for a BoundUnaryOp based on operator and operand type.
binder::UnaryBatchFn resolveUnaryBatchFn(cypher::UnaryOperator op, binder::BoundTypeKind operand_type);

} // namespace function
} // namespace eugraph
