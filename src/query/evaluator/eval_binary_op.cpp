#include "query/evaluator/vectorized_evaluator.hpp"

namespace eugraph {
namespace compute {

void VectorizedEvaluator::evalBinaryOp(const binder::BoundBinaryOp& op, const DataChunk& input, Column& result,
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
            if (left.column->isNull(i) || right.column->isNull(i)) {
                result.setNull(i);
                continue;
            }
            Value lv = left.column->getValue(i);
            Value rv = right.column->getValue(i);
            if (!std::holds_alternative<ListValue>(rv)) {
                result.setNull(i);
                continue;
            }
            const auto& list = std::get<ListValue>(rv);
            bool found = false;
            for (const auto& elem : list.elements) {
                if (!eugraph::isNull(elem.value) && elem.value == lv) {
                    found = true;
                    break;
                }
            }
            result.setValue(i, Value(found));
        }
        return;
    }

    if (op.batch_fn) {
        op.batch_fn(*left.column, *right.column, result, count);
    }
}

} // namespace compute
} // namespace eugraph
