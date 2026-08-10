#include "query/evaluator/vectorized_evaluator.hpp"

#include "query/planner/bound_expression/bound_slice.hpp"

namespace eugraph {
namespace compute {

namespace {
// Apply Cypher slice semantics with negative-index normalization and clamping.
// `from_idx`/`to_idx` may be negative (counted from end) and are clamped to
// the valid range. If from > to after normalization, the result is empty.
void finishSlice(const ListValue& lv, int64_t from_idx, int64_t to_idx, Column& result, size_t i) {
    int64_t lv_size = static_cast<int64_t>(lv.elements.size());
    if (from_idx < 0)
        from_idx += lv_size;
    if (to_idx < 0)
        to_idx += lv_size;
    if (from_idx < 0)
        from_idx = 0;
    if (from_idx > lv_size)
        from_idx = lv_size;
    if (to_idx > lv_size)
        to_idx = lv_size;
    if (to_idx < from_idx)
        to_idx = from_idx;
    ListValue result_lv;
    for (int64_t j = from_idx; j < to_idx; ++j) {
        result_lv.elements.push_back(lv.elements[static_cast<size_t>(j)]);
    }
    result.setValue(i, Value(std::move(result_lv)));
}
} // namespace

void VectorizedEvaluator::evalSlice(const binder::BoundSlice& slice, const DataChunk& input, Column& result,
                                    size_t count) {
    auto list_eval = evaluateInternal(slice.list, input);
    if (!list_eval.column)
        return;

    for (size_t i = 0; i < count; ++i) {
        if (list_eval.column->isNull(i)) {
            result.setNull(i);
            continue;
        }
        Value val = list_eval.column->getValue(i);
        if (!std::holds_alternative<ListValue>(val)) {
            result.setNull(i);
            continue;
        }
        const auto& lv = std::get<ListValue>(val);

        // Cypher: if either bound is null, the slice result is null.
        if (slice.from) {
            auto from_eval = evaluateInternal(*slice.from, input);
            if (!from_eval.column || from_eval.column->isNull(i)) {
                result.setNull(i);
                continue;
            }
            Value fv = from_eval.column->getValue(i);
            if (!std::holds_alternative<int64_t>(fv)) {
                result.setNull(i);
                continue;
            }
            int64_t from_idx = std::get<int64_t>(fv);
            if (slice.to) {
                auto to_eval = evaluateInternal(*slice.to, input);
                if (!to_eval.column || to_eval.column->isNull(i)) {
                    result.setNull(i);
                    continue;
                }
                Value tv = to_eval.column->getValue(i);
                if (!std::holds_alternative<int64_t>(tv)) {
                    result.setNull(i);
                    continue;
                }
                int64_t to_idx = std::get<int64_t>(tv);
                finishSlice(lv, from_idx, to_idx, result, i);
                continue;
            }
            finishSlice(lv, from_idx, static_cast<int64_t>(lv.elements.size()), result, i);
            continue;
        }
        if (slice.to) {
            auto to_eval = evaluateInternal(*slice.to, input);
            if (!to_eval.column || to_eval.column->isNull(i)) {
                result.setNull(i);
                continue;
            }
            Value tv = to_eval.column->getValue(i);
            if (!std::holds_alternative<int64_t>(tv)) {
                result.setNull(i);
                continue;
            }
            int64_t to_idx = std::get<int64_t>(tv);
            finishSlice(lv, 0, to_idx, result, i);
            continue;
        }
        finishSlice(lv, 0, static_cast<int64_t>(lv.elements.size()), result, i);
    }
}

} // namespace compute
} // namespace eugraph
