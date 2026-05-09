#pragma once

#include "compute_service/parser/ast.hpp"
#include "compute_service/planner/bound_expression/bound_expression_fwd.hpp"
#include "compute_service/planner/bound_type.hpp"

#include <functional>

namespace eugraph {
struct Column;
namespace binder {

/// Batch unary operation: processes all rows at once, replacing per-row switch dispatch.
using UnaryBatchFn = std::function<void(const Column&, Column&, size_t)>;

/// Bound unary operation with resolved operand type and result type.
struct BoundUnaryOp {
    cypher::UnaryOperator op;
    BoundExpression operand;
    BoundType result_type;
    UnaryBatchFn batch_fn;

    BoundUnaryOp() : result_type(BoundType::Any()) {}
};

} // namespace binder
} // namespace eugraph
