#include "query/evaluator/vectorized_evaluator.hpp"

#include "query/planner/bound_expression/bound_map.hpp"

namespace eugraph {
namespace compute {

void VectorizedEvaluator::evalMap(const binder::BoundMap& map, const DataChunk& input, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        MapValue mv;
        for (const auto& [key, expr] : map.entries) {
            auto elem_eval = evaluateInternal(expr, input);
            if (elem_eval.column && !elem_eval.column->isNull(i)) {
                mv.entries.push_back({key, ValueStorage{elem_eval.column->getValue(i)}});
            } else {
                mv.entries.push_back({key, ValueStorage{Value{}}});
            }
        }
        result.setValue(i, Value(std::move(mv)));
    }
}

} // namespace compute
} // namespace eugraph
