#include "query/evaluator/expression_evaluator.hpp"

#include "query/dataset/data_chunk.hpp"

#include <algorithm>

namespace eugraph {
namespace compute {

void ExpressionEvaluator::evalReduce(const binder::BoundReduceExpr& reduce, const DataChunk& input, Column& result,
                                     size_t count) {
    auto init_eval = evaluateInternal(reduce.init, input);
    auto list_eval = evaluateInternal(reduce.list_expr, input);
    const size_t total_cols = std::max({input.columns.size(), static_cast<size_t>(reduce.accumulator_column_index) + 1,
                                        static_cast<size_t>(reduce.loop_column_index) + 1});

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
        for (const auto& elem : (*std::get<ListValuePtr>(list_val)).elements) {
            DataChunk temp;
            temp.columns.resize(total_cols);
            temp.count = 1;
            temp.sel = SelectionVector::identity(1);
            for (size_t c = 0; c < input.columns.size(); ++c)
                temp.columns[c] = Column::constant(input.columns[c].getValue(i));
            for (size_t c = input.columns.size(); c < total_cols; ++c)
                temp.columns[c] = Column::constant(Value{});
            temp.columns[reduce.accumulator_column_index] = Column::constant(acc);
            temp.columns[reduce.loop_column_index] = Column::constant(elem.value);

            auto body = evaluateInternal(reduce.expr, temp);
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
