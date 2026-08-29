#include "query/evaluator/columnar_kernels.hpp"
#include "query/evaluator/expression_evaluator.hpp"

namespace eugraph {
namespace compute {
using namespace eugraph::compute::detail;

void ExpressionEvaluator::evalUnaryOp(const binder::BoundUnaryOp& op, const DataChunk& input, Column& result,
                                      size_t count) {
    auto operand = evaluateInternal(op.operand, input);
    if (!operand.column)
        return;

    const detail::RowIndex rows = !operand.is_temp ? chunkRows(input) : detail::RowIndex{nullptr, count};
    if (tryEvaluateTypedUnaryColumn(op, *operand.column, rows, result))
        return;

    if (op.fallback_fn) {
        op.fallback_fn(*operand.column, result, count);
    }
}

bool typedNotEval(const Column& src, const detail::RowIndex& rows, Column& result) {
    if (result.form != VectorForm::FLAT || !result.buffer)
        return false;
    if (src.form != VectorForm::FLAT && src.form != VectorForm::CONSTANT && src.form != VectorForm::DICTIONARY)
        return false;
    if (!src.buffer && src.form != VectorForm::CONSTANT)
        return false;
    const size_t n = rows.count;
    result.reserve(n);
    uint8_t* out = result.buffer->bool_data.data();
    if (src.form == VectorForm::CONSTANT) {
        if (src.isNull(0)) {
            for (size_t i = 0; i < n; ++i)
                result.setNull(i);
        } else {
            const bool* v = std::get_if<bool>(&src.constant_value);
            if (!v)
                return false;
            for (size_t i = 0; i < n; ++i)
                out[i] = *v ? 0 : 1;
        }
        return true;
    }
    const uint8_t* in = src.buffer->bool_data.data();
    for (size_t i = 0; i < n; ++i) {
        const size_t p = detail::dictPhysical(src, rows, i);
        if (src.buffer->isNull(p)) {
            result.setNull(i);
        } else {
            out[i] = in[p] ? 0 : 1;
        }
    }
    return true;
}

bool typedIsNullEval(const Column& src, const detail::RowIndex& rows, Column& result, bool invert) {
    if (result.form != VectorForm::FLAT || !result.buffer)
        return false;
    if (src.form != VectorForm::FLAT && src.form != VectorForm::CONSTANT && src.form != VectorForm::DICTIONARY)
        return false;
    if (!src.buffer && src.form != VectorForm::CONSTANT)
        return false;
    const size_t n = rows.count;
    result.reserve(n);
    uint8_t* out = result.buffer->bool_data.data();
    if (src.form == VectorForm::CONSTANT) {
        const bool null_value = src.isNull(0);
        for (size_t i = 0; i < n; ++i)
            out[i] = (null_value != invert) ? 1 : 0;
        return true;
    }
    for (size_t i = 0; i < n; ++i) {
        const size_t p = detail::dictPhysical(src, rows, i);
        const bool null_value = src.buffer->isNull(p);
        out[i] = (null_value != invert) ? 1 : 0;
    }
    return true;
}

bool ExpressionEvaluator::tryEvaluateTypedUnaryColumn(const binder::BoundUnaryOp& op, const Column& operand,
                                                      const detail::RowIndex& rows, Column& result) {
    if (op.op == cypher::UnaryOperator::IS_NULL)
        return typedIsNullEval(operand, rows, result, false);
    if (op.op == cypher::UnaryOperator::IS_NOT_NULL)
        return typedIsNullEval(operand, rows, result, true);

    using BTK = binder::BoundTypeKind;
    if (op.op == cypher::UnaryOperator::NEGATE) {
        if (operand.type == BTK::INT64 && result.type == BTK::INT64)
            return typedUnaryEval<int64_t, int64_t, NegOp>(operand, rows, result);
        if (operand.type == BTK::DOUBLE && result.type == BTK::DOUBLE)
            return typedUnaryEval<double, double, NegOp>(operand, rows, result);
    }
    if (op.op == cypher::UnaryOperator::PLUS) {
        if (operand.type == BTK::INT64 && result.type == BTK::INT64)
            return typedUnaryEval<int64_t, int64_t, PlusOp>(operand, rows, result);
        if (operand.type == BTK::DOUBLE && result.type == BTK::DOUBLE)
            return typedUnaryEval<double, double, PlusOp>(operand, rows, result);
    }
    if (op.op == cypher::UnaryOperator::NOT && operand.type == BTK::BOOL && result.type == BTK::BOOL)
        return typedNotEval(operand, rows, result);
    return false;
}

} // namespace compute
} // namespace eugraph
