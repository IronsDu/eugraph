#include "query/evaluator/vectorized_evaluator.hpp"

namespace eugraph {
namespace compute {

void VectorizedEvaluator::evalListComprehension(const binder::BoundListComprehension& lc, const DataChunk& input,
                                                Column& result, size_t count) {
    auto list_eval = evaluateInternal(lc.list_expr, input);

    size_t total_cols = std::max(static_cast<size_t>(lc.loop_column_index) + 1, input.columns.size() + 1);

    for (size_t i = 0; i < count; ++i) {
        ListValue result_list;

        if (!list_eval.column || list_eval.column->isNull(i)) {
            result.setValue(i, Value(std::move(result_list)));
            continue;
        }

        Value list_val = list_eval.column->getValue(i);
        if (!std::holds_alternative<ListValue>(list_val)) {
            result.setValue(i, Value(std::move(result_list)));
            continue;
        }

        const auto& lv = std::get<ListValue>(list_val);

        for (const auto& elem_storage : lv.elements) {
            Value elem_val = elem_storage.value;

            DataChunk temp_chunk;
            temp_chunk.columns.resize(total_cols);
            temp_chunk.count = 1;
            temp_chunk.sel = SelectionVector::identity(1);

            for (size_t c = 0; c < input.columns.size(); ++c) {
                temp_chunk.columns[c] = Column::constant(input.columns[c].getValue(i));
            }
            for (size_t c = input.columns.size(); c < total_cols; ++c) {
                temp_chunk.columns[c] = Column::constant(Value{});
            }
            temp_chunk.columns[lc.loop_column_index] = Column::constant(elem_val);

            // Check WHERE predicate if present
            bool elem_passes = true;
            if (lc.where_pred) {
                if (isNull(elem_val)) {
                    size_t col_count_before = temp_columns_.size();
                    evaluateInternal(*lc.where_pred, temp_chunk);
                    if (temp_columns_.size() > col_count_before && !temp_columns_[col_count_before].isNull(0)) {
                        Value v = temp_columns_[col_count_before].getValue(0);
                        if (std::holds_alternative<bool>(v))
                            elem_passes = std::get<bool>(v);
                        else
                            continue;
                    } else {
                        continue;
                    }
                } else {
                    std::vector<bool> pred_result;
                    evaluatePredicate(*lc.where_pred, temp_chunk, pred_result);
                    elem_passes = !pred_result.empty() && pred_result[0];
                }
            }

            if (!elem_passes)
                continue;

            // Evaluate projection on matching element
            auto proj_eval = evaluateInternal(lc.projection, temp_chunk);
            if (proj_eval.column) {
                result_list.elements.push_back(ValueStorage{proj_eval.column->getValue(0)});
            } else {
                result_list.elements.push_back(ValueStorage{Value{}});
            }
        }

        result.setValue(i, Value(std::move(result_list)));
    }
}

} // namespace compute
} // namespace eugraph
