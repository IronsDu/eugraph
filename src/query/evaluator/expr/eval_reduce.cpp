#include "query/evaluator/expression_evaluator.hpp"

#include "query/dataset/data_chunk.hpp"

#include <algorithm>

namespace eugraph {
namespace compute {

void ExpressionEvaluator::evalReduce(const binder::BoundReduceExpr& reduce, const DataChunk& input, Column& result,
                                     size_t count) {
    auto init_eval = evaluateInternal(reduce.init, input);
    auto list_eval = evaluateInternal(reduce.list_expr, input);
    const uint32_t acc_pos = static_cast<uint32_t>(input.columns.size());
    const uint32_t loop_pos = acc_pos + 1;
    const uint32_t index_pos = acc_pos + 2;
    const size_t total_cols = static_cast<size_t>(index_pos) + 1;

    for (size_t i = 0; i < count; ++i) {
        Value acc = (init_eval.column && !init_eval.column->isNull(i)) ? init_eval.column->getValue(i) : Value{};
        if (isNull(acc)) {
            result.setNull(i);
            continue;
        }
        if (!list_eval.column || list_eval.column->isNull(i)) {
            result.setNull(i);
            continue;
        }
        Value list_val = list_eval.column->getValue(i);
        if (!std::holds_alternative<ListValuePtr>(list_val)) {
            result.setNull(i);
            continue;
        }
        size_t elem_index = 0;
        for (const auto& elem : (*std::get<ListValuePtr>(list_val)).elements) {
            DataChunk temp;
            temp.columns.resize(total_cols);
            temp.count = 1;
            temp.sel = SelectionVector::identity(1);
            for (size_t c = 0; c < input.columns.size(); ++c)
                temp.columns[c] = Column::constant(input.columns[c].getValue(i));
            for (size_t c = input.columns.size(); c < total_cols; ++c)
                temp.columns[c] = Column::constant(Value{});
            temp.columns[acc_pos] = Column::constant(acc);
            temp.columns[loop_pos] = Column::constant(elem.value);
            temp.columns[index_pos] = Column::constant(Value{static_cast<int64_t>(elem_index)});
            ++elem_index;

            auto previous_overrides = temp_column_overrides_;
            temp_column_overrides_ = {
                {reduce.accumulator_slot, acc_pos}, {reduce.loop_slot, loop_pos}, {reduce.index_slot, index_pos}};
            auto body = evaluateInternal(reduce.expr, temp);
            temp_column_overrides_ = std::move(previous_overrides);
            if (!body.column || body.column->isNull(0)) {
                acc = Value{};
                break;
            }
            acc = body.column->getValue(0);
        }
        if (isNull(acc))
            result.setNull(i);
        else
            result.setValue(i, std::move(acc));
    }
}

} // namespace compute
} // namespace eugraph
