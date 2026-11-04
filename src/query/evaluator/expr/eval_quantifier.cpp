#include "query/evaluator/expression_evaluator.hpp"

namespace eugraph {
namespace compute {

void ExpressionEvaluator::evalQuantifierExpr(QuantifierKind kind, uint32_t loop_column_index,
                                             const binder::BoundExpression& list_expr,
                                             const std::optional<binder::BoundExpression>& where_pred,
                                             const DataChunk& input, Column& result, size_t count) {
    auto list_eval = evaluateInternal(list_expr, input);
    const size_t temp_mark = tempMark();

    size_t total_cols = std::max(static_cast<size_t>(loop_column_index) + 1, input.columns.size() + 1);

    // Reuse one 1-row chunk per evaluation depth; only the loop column changes
    // between elements of the same outer row.
    auto& s = scratch();
    DataChunk& temp_chunk = s.single_row_chunk;
    if (temp_chunk.columns.size() < total_cols)
        temp_chunk.columns.resize(total_cols);
    temp_chunk.count = 1;
    temp_chunk.sel = SelectionVector::identity(1);

    for (size_t i = 0; i < count; ++i) {
        // Initialised to the "empty input" answer for this kind, so the
        // variable is never read before being written even if the enum ever
        // gains a value the switch below does not handle. GCC additionally
        // reported a possible use-uninitialised read at
        // `at_initial = !final_result` (the ANY case) even though the switch
        // covers every current enumerator; the initialiser settles that too and
        // costs nothing.
        bool final_result = (kind == QuantifierKind::ALL || kind == QuantifierKind::NONE);
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
        if (!std::holds_alternative<ListValuePtr>(list_val)) {
            final_result = (kind == QuantifierKind::ALL || kind == QuantifierKind::NONE);
            result.setValue(i, Value(final_result));
            continue;
        }

        const auto& lv = (*std::get<ListValuePtr>(list_val));

        if (lv.elements.empty()) {
            final_result = (kind == QuantifierKind::ALL || kind == QuantifierKind::NONE);
            result.setValue(i, Value(final_result));
            continue;
        }

        // Input columns are constant for the whole outer row; set them once.
        for (size_t c = 0; c < input.columns.size(); ++c)
            temp_chunk.columns[c].resetConstant(binder::BoundTypeKind::NULL_TYPE, input.columns[c].getValue(i));
        for (size_t c = input.columns.size(); c < total_cols; ++c)
            temp_chunk.columns[c].resetConstant(binder::BoundTypeKind::NULL_TYPE, Value{});

        bool saw_inconclusive = false;
        for (const auto& elem_storage : lv.elements) {
            resetToTempMark(temp_mark);
            Value elem_val = elem_storage.value;
            temp_chunk.columns[loop_column_index].resetConstant(binder::BoundTypeKind::NULL_TYPE, elem_val);

            bool elem_passes = true;
            if (where_pred) {
                auto pred = evaluatePredicateOne(*where_pred, temp_chunk);
                if (isNull(elem_val)) {
                    // A NULL element with an unknown predicate is inconclusive;
                    // a known bool still participates normally (old behaviour).
                    if (!pred) {
                        saw_inconclusive = true;
                        continue;
                    }
                    elem_passes = *pred;
                } else {
                    elem_passes = pred.value_or(false);
                }
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
                at_initial = final_result;
                break;
            case QuantifierKind::ANY:
                at_initial = !final_result;
                break;
            case QuantifierKind::NONE:
                at_initial = final_result;
                break;
            case QuantifierKind::SINGLE:
                at_initial = (single_match_count <= 1);
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
