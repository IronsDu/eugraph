#include "query/evaluator/expression_evaluator.hpp"

namespace eugraph {
namespace compute {

void ExpressionEvaluator::evalListComprehension(const binder::BoundListComprehension& lc, const DataChunk& input,
                                                Column& result, size_t count) {
    auto list_eval = evaluateInternal(lc.list_expr, input);
    const size_t temp_mark = tempMark();

    const size_t total_cols = std::max(static_cast<size_t>(lc.loop_column_index) + 1, input.columns.size() + 1);

    // One reused 1-row chunk per evaluation depth. The old shape built a fresh
    // DataChunk and copied every input column for *each element*; only the loop
    // column changes between elements of the same outer row.
    auto& s = scratch();
    DataChunk& temp_chunk = s.single_row_chunk;
    if (temp_chunk.columns.size() < total_cols)
        temp_chunk.columns.resize(total_cols);
    temp_chunk.count = 1;
    temp_chunk.sel = SelectionVector::identity(1);

    for (size_t i = 0; i < count; ++i) {
        ListValue result_list;
        if (!list_eval.column || list_eval.column->isNull(i)) {
            result.setValue(i, Value(mk<ListValue>(std::move(result_list))));
            continue;
        }

        Value list_val = list_eval.column->getValue(i);
        if (!std::holds_alternative<ListValuePtr>(list_val)) {
            result.setValue(i, Value(mk<ListValue>(std::move(result_list))));
            continue;
        }

        const auto& lv = (*std::get<ListValuePtr>(list_val));
        result_list.elements.reserve(lv.elements.size());

        // Input columns are constant for the whole outer row; set them once.
        for (size_t c = 0; c < input.columns.size(); ++c)
            temp_chunk.columns[c].resetConstant(binder::BoundTypeKind::NULL_TYPE, input.columns[c].getValue(i));
        for (size_t c = input.columns.size(); c < total_cols; ++c)
            temp_chunk.columns[c].resetConstant(binder::BoundTypeKind::NULL_TYPE, Value{});

        for (const auto& elem_storage : lv.elements) {
            // Predicate/projection temporaries are dead after each element;
            // recycle them instead of growing the temp-column pool per element.
            resetToTempMark(temp_mark);
            Value elem_val = elem_storage.value;
            temp_chunk.columns[lc.loop_column_index].resetConstant(binder::BoundTypeKind::NULL_TYPE, elem_val);

            bool elem_passes = true;
            if (lc.where_pred)
                elem_passes = evaluatePredicateOne(*lc.where_pred, temp_chunk).value_or(false);
            if (!elem_passes)
                continue;

            resetToTempMark(temp_mark);
            auto proj_eval = evaluateInternal(lc.projection, temp_chunk);
            if (proj_eval.column) {
                result_list.elements.push_back(ValueStorage{proj_eval.column->getValue(0)});
            } else {
                result_list.elements.push_back(ValueStorage{Value{}});
            }
        }

        result.setValue(i, Value(mk<ListValue>(std::move(result_list))));
    }
}

} // namespace compute
} // namespace eugraph
