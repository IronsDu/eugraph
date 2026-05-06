#pragma once

#include "compute_service/binder/bound_expression/bound_expression_fwd.hpp"
#include "compute_service/binder/bound_type.hpp"
#include "compute_service/parser/ast.hpp"

#include <cstdint>

namespace eugraph {
namespace binder {

/// Bound binary operation with resolved operand types and result type.
struct BoundBinaryOp {
    cypher::BinaryOperator op;
    BoundExpression left;
    BoundExpression right;
    BoundType result_type;

    BoundBinaryOp() : result_type(BoundType::Any()) {}
};

} // namespace binder
} // namespace eugraph
