#include "query/evaluator/vectorized_evaluator.hpp"

#include "query/planner/bound_expression/bound_subscript.hpp"

namespace eugraph {
namespace compute {

void VectorizedEvaluator::evalSubscript(const binder::BoundSubscript& sub, const DataChunk& input, Column& result,
                                        size_t count) {
    auto list_eval = evaluateInternal(sub.list, input);
    auto idx_eval = evaluateInternal(sub.index, input);
    if (!list_eval.column || !idx_eval.column)
        return;

    for (size_t i = 0; i < count; ++i) {
        if (list_eval.column->isNull(i) || idx_eval.column->isNull(i)) {
            result.setNull(i);
            continue;
        }
        Value list_val = list_eval.column->getValue(i);
        Value idx_val = idx_eval.column->getValue(i);

        if (std::holds_alternative<ListValue>(list_val) && std::holds_alternative<int64_t>(idx_val)) {
            const auto& lv = std::get<ListValue>(list_val);
            int64_t idx = std::get<int64_t>(idx_val);
            if (idx < 0)
                idx += static_cast<int64_t>(lv.elements.size());
            if (idx >= 0 && static_cast<size_t>(idx) < lv.elements.size()) {
                result.setValue(i, lv.elements[static_cast<size_t>(idx)].value);
            } else {
                result.setNull(i);
            }
        } else if (std::holds_alternative<MapValue>(list_val) && std::holds_alternative<std::string>(idx_val)) {
            const auto& mv = std::get<MapValue>(list_val);
            const auto& key = std::get<std::string>(idx_val);
            bool found = false;
            for (const auto& [k, v] : mv.entries) {
                if (k == key) {
                    result.setValue(i, v.value);
                    found = true;
                    break;
                }
            }
            if (!found)
                result.setNull(i);
        } else {
            result.setNull(i);
        }
    }
}

} // namespace compute
} // namespace eugraph
