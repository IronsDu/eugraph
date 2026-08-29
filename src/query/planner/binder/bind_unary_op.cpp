#include "query/planner/binder/bind_unary_op.hpp"

#include "query/evaluator/columnar_kernels.hpp"

namespace eugraph {
namespace binder {

using namespace eugraph::compute::detail;

binder::UnaryFallbackFn resolveUnaryFallbackFn(cypher::UnaryOperator op, binder::BoundTypeKind operand_type) {
    using UO = cypher::UnaryOperator;
    using BTK = binder::BoundTypeKind;

    switch (op) {
    case UO::NOT:
        return boolNotBatch;
    case UO::NEGATE:
        if (operand_type == BTK::INT64)
            return int64NegateBatch;
        if (operand_type == BTK::DOUBLE)
            return doubleNegateBatch;
        return nullptr;
    case UO::IS_NULL:
        return isNullBatch;
    case UO::IS_NOT_NULL:
        return isNotNullBatch;
    default:
        return nullptr;
    }
}

} // namespace binder
} // namespace eugraph
