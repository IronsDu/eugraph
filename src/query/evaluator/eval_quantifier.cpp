#include "query/evaluator/vectorized_evaluator.hpp"

namespace eugraph {
namespace compute {

void VectorizedEvaluator::evalQuantifierExpr(QuantifierKind kind, uint32_t loop_column_index,
                                             const binder::BoundExpression& list_expr,
                                             const std::optional<binder::BoundExpression>& where_pred,
                                             const DataChunk& input, Column& result, size_t count) {
    auto list_eval = evaluateInternal(list_expr, input);

    size_t total_cols = std::max(static_cast<size_t>(loop_column_index) + 1, input.columns.size() + 1);

    for (size_t i = 0; i < count; ++i) {
        bool final_result;
        int single_match_count = 0;

        switch (kind) {
        case QuantifierKind::ALL:
            final_result = true;
            break;
        case QuantifierKind::ANY:
            final_result = false;
            break;
        case QuantifierKind::NONE:
            final_result = true;
            break;
        case QuantifierKind::SINGLE:
            final_result = false;
            break;
        }

        if (!list_eval.column || list_eval.column->isNull(i)) {
            final_result = (kind == QuantifierKind::ALL || kind == QuantifierKind::NONE);
            result.setValue(i, Value(final_result));
            continue;
        }

        Value list_val = list_eval.column->getValue(i);
        if (!std::holds_alternative<ListValue>(list_val)) {
            final_result = (kind == QuantifierKind::ALL || kind == QuantifierKind::NONE);
            result.setValue(i, Value(final_result));
            continue;
        }

        const auto& lv = std::get<ListValue>(list_val);

        if (lv.elements.empty()) {
            final_result = (kind == QuantifierKind::ALL || kind == QuantifierKind::NONE);
            result.setValue(i, Value(final_result));
            continue;
        }

        bool saw_inconclusive = false;
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
            temp_chunk.columns[loop_column_index] = Column::constant(elem_val);

            bool elem_passes = false;
            bool inconclusive = false;
            if (where_pred) {
                auto pred_eval = evaluateInternal(*where_pred, temp_chunk);
                if (pred_eval.column && !pred_eval.column->isNull(0)) {
                    Value v = pred_eval.column->getValue(0);
                    if (std::holds_alternative<bool>(v))
                        elem_passes = std::get<bool>(v);
                } else {
                    inconclusive = true;
                }
            }

            if (inconclusive) {
                saw_inconclusive = true;
                continue;
            }

            switch (kind) {
            case QuantifierKind::ALL:
                if (!elem_passes) {
                    final_result = false;
                    goto done;
                }
                break;
            case QuantifierKind::ANY:
                if (elem_passes) {
                    final_result = true;
                    goto done;
                }
                break;
            case QuantifierKind::NONE:
                if (elem_passes) {
                    final_result = false;
                    goto done;
                }
                break;
            case QuantifierKind::SINGLE:
                if (elem_passes) {
                    ++single_match_count;
                    if (single_match_count > 1) {
                        final_result = false;
                        goto done;
                    }
                }
                break;
            }
        }

    done:
        if (kind == QuantifierKind::SINGLE) {
            final_result = (single_match_count == 1);
        }
        if (saw_inconclusive) {
            bool at_initial = false;
            switch (kind) {
            case QuantifierKind::ALL:
                at_initial = final_result; // initial=true
                break;
            case QuantifierKind::ANY:
                at_initial = !final_result; // initial=false
                break;
            case QuantifierKind::NONE:
                at_initial = final_result; // initial=true
                break;
            case QuantifierKind::SINGLE:
                at_initial = (single_match_count == 0 && !final_result); // initial=false
                break;
            }
            if (at_initial)
                result.setNull(i);
            else
                result.setValue(i, Value(final_result));
        } else {
            result.setValue(i, Value(final_result));
        }
    }
}

} // namespace compute
} // namespace eugraph
