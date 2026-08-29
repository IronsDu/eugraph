#include "query/evaluator/columnar_kernels.hpp"
#include "query/evaluator/expression_evaluator.hpp"

#include <spdlog/spdlog.h>

namespace eugraph {
namespace compute {
using namespace eugraph::compute::detail;

void ExpressionEvaluator::evalBinaryOp(const binder::BoundBinaryOp& op, const DataChunk& input, Column& result,
                                       size_t count) {
    auto left = evaluateInternal(op.left, input);
    auto right = evaluateInternal(op.right, input);

    if (!left.column || !right.column)
        return;

    using BO = cypher::BinaryOperator;

    if (op.op == BO::XOR) {
        for (size_t i = 0; i < count; ++i) {
            Value lv = left.column->getValue(i);
            Value rv = right.column->getValue(i);
            auto lb = std::get_if<bool>(&lv);
            auto rb = std::get_if<bool>(&rv);
            if (!lb || !rb) {
                result.setNull(i);
            } else {
                result.setValue(i, Value(*lb != *rb));
            }
        }
        return;
    }

    if (op.op == BO::IN) {
        for (size_t i = 0; i < count; ++i) {
            if (right.column->isNull(i)) {
                result.setNull(i);
                continue;
            }
            Value rv = right.column->getValue(i);
            if (!std::holds_alternative<ListValue>(rv)) {
                result.setNull(i);
                continue;
            }
            const auto& list = std::get<ListValue>(rv);
            // null IN [] → false (null is definitely not in an empty list)
            if (left.column->isNull(i)) {
                if (list.elements.empty()) {
                    result.setValue(i, Value(false));
                } else {
                    result.setNull(i);
                }
                continue;
            }
            Value lv = left.column->getValue(i);
            bool found = false;
            bool saw_null = false;
            for (const auto& elem : list.elements) {
                // Three-valued equality: nested nulls (e.g. `[null] IN [[null]]`)
                // must propagate to unknown, not collapse to a definitive answer.
                auto cmp = valueEquals(elem.value, lv);
                if (!cmp) {
                    saw_null = true;
                    continue;
                }
                if (*cmp) {
                    found = true;
                    break;
                }
            }
            if (found) {
                result.setValue(i, Value(true));
            } else if (saw_null) {
                result.setNull(i);
            } else {
                result.setValue(i, Value(false));
            }
        }
        return;
    }

    // The operand columns are already evaluated; run the typed fast path on
    // their runtime representation when possible.
    const detail::RowIndex rows =
        (!left.is_temp && !right.is_temp) ? chunkRows(input) : detail::RowIndex{nullptr, count};
    if (tryEvaluateTypedBinaryColumns(op, *left.column, *right.column, rows, result))
        return;

    if (op.fallback_fn) {
        op.fallback_fn(*left.column, *right.column, result, count);
    }
}

bool ExpressionEvaluator::tryEvaluateTypedBinaryColumns(const binder::BoundBinaryOp& op, const Column& lhs,
                                                        const Column& rhs, const detail::RowIndex& rows,
                                                        Column& result) {
    using BTK = binder::BoundTypeKind;

    if (lhs.type == BTK::INT64 && rhs.type == BTK::INT64) {
        if (op.op == cypher::BinaryOperator::ADD)
            return typedBinaryEval<int64_t, int64_t, AddOp>(lhs, rhs, rows, result);
        if (op.op == cypher::BinaryOperator::SUB)
            return typedBinaryEval<int64_t, int64_t, SubOp>(lhs, rhs, rows, result);
        if (op.op == cypher::BinaryOperator::MUL)
            return typedBinaryEval<int64_t, int64_t, MulOp>(lhs, rhs, rows, result);
        if (op.op == cypher::BinaryOperator::LT)
            return typedBinaryEval<int64_t, uint8_t, LtOp>(lhs, rhs, rows, result);
        if (op.op == cypher::BinaryOperator::GT)
            return typedBinaryEval<int64_t, uint8_t, GtOp>(lhs, rhs, rows, result);
        if (op.op == cypher::BinaryOperator::LTE)
            return typedBinaryEval<int64_t, uint8_t, LeOp>(lhs, rhs, rows, result);
        if (op.op == cypher::BinaryOperator::GTE)
            return typedBinaryEval<int64_t, uint8_t, GteOp>(lhs, rhs, rows, result);
        return false;
    }
    if (lhs.type == BTK::DOUBLE && rhs.type == BTK::DOUBLE) {
        if (op.op == cypher::BinaryOperator::ADD)
            return typedBinaryEval<double, double, AddOp>(lhs, rhs, rows, result);
        if (op.op == cypher::BinaryOperator::SUB)
            return typedBinaryEval<double, double, SubOp>(lhs, rhs, rows, result);
        if (op.op == cypher::BinaryOperator::MUL)
            return typedBinaryEval<double, double, MulOp>(lhs, rhs, rows, result);
        if (op.op == cypher::BinaryOperator::LT)
            return typedBinaryEval<double, uint8_t, LtOp>(lhs, rhs, rows, result);
        if (op.op == cypher::BinaryOperator::GT)
            return typedBinaryEval<double, uint8_t, GtOp>(lhs, rhs, rows, result);
        if (op.op == cypher::BinaryOperator::LTE)
            return typedBinaryEval<double, uint8_t, LeOp>(lhs, rhs, rows, result);
        if (op.op == cypher::BinaryOperator::GTE)
            return typedBinaryEval<double, uint8_t, GteOp>(lhs, rhs, rows, result);
        return false;
    }
    return false;
}

} // namespace compute
} // namespace eugraph
