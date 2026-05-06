#pragma once

#include "compute_service/binder/bound_expression/bound_expression_fwd.hpp"
#include "compute_service/binder/bound_type.hpp"
#include "compute_service/parser/ast.hpp"

namespace eugraph {
namespace binder {

/// Bound unary operation with resolved operand type and result type.
struct BoundUnaryOp {
    cypher::UnaryOperator op;
    BoundExpression operand;
    BoundType result_type;

    BoundUnaryOp() : result_type(BoundType::Any()) {}
};

} // namespace binder
} // namespace eugraph
