#include "query/evaluator/vectorized_evaluator.hpp"

#include "query/planner/bound_expression/bound_slice.hpp"

namespace eugraph {
namespace compute {

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
        size_t lv_size = lv.elements.size();

        int64_t from_idx = 0;
        int64_t to_idx = static_cast<int64_t>(lv_size);
        if (slice.from) {
            auto from_eval = evaluateInternal(*slice.from, input);
            if (from_eval.column && !from_eval.column->isNull(i)) {
                Value fv = from_eval.column->getValue(i);
                if (std::holds_alternative<int64_t>(fv))
                    from_idx = std::get<int64_t>(fv);
            }
        }
        if (slice.to) {
            auto to_eval = evaluateInternal(*slice.to, input);
            if (to_eval.column && !to_eval.column->isNull(i)) {
                Value tv = to_eval.column->getValue(i);
                if (std::holds_alternative<int64_t>(tv))
                    to_idx = std::get<int64_t>(tv);
            }
        }

        if (from_idx < 0)
            from_idx = 0;
        if (to_idx < 0)
            to_idx = 0;
        if (from_idx > static_cast<int64_t>(lv_size))
            from_idx = static_cast<int64_t>(lv_size);
        if (to_idx > static_cast<int64_t>(lv_size))
            to_idx = static_cast<int64_t>(lv_size);

        ListValue result_lv;
        for (int64_t j = from_idx; j < to_idx; ++j) {
            result_lv.elements.push_back(lv.elements[static_cast<size_t>(j)]);
        }
        result.setValue(i, Value(std::move(result_lv)));
    }
}

} // namespace compute
} // namespace eugraph
