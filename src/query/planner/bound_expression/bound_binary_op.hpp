#pragma once

#include "query/parser/ast.hpp"
#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/bound_type.hpp"

#include <cstdint>
#include <functional>

namespace eugraph {
struct Column;
namespace binder {

/// Batch binary operation: processes all rows at once, replacing per-row switch dispatch.
using BinaryBatchFn = std::function<void(const Column&, const Column&, Column&, size_t)>;

/// Bound binary operation with resolved operand types and result type.
struct BoundBinaryOp {
    cypher::BinaryOperator op;
    BoundExpression left;
    BoundExpression right;
    BoundType result_type;
    BinaryBatchFn batch_fn;

    BoundBinaryOp() : result_type(BoundType::Any()) {}
};

} // namespace binder
} // namespace eugraph
