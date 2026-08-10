#include "query/evaluator/vectorized_evaluator.hpp"

#include "query/planner/bound_expression/bound_list.hpp"

namespace eugraph {
namespace compute {

VectorizedEvaluator::EvalResult VectorizedEvaluator::evalList(const binder::BoundList& list, const DataChunk& input) {
    size_t count = input.numRows();
    auto& col = acquireTempColumn(binder::BoundTypeKind::LIST, count);
    for (size_t i = 0; i < count; ++i) {
        ListValue lv;
        for (const auto& elem : list.elements) {
            auto elem_eval = evaluateInternal(elem, input);
            if (elem_eval.column && !elem_eval.column->isNull(i)) {
                lv.elements.push_back(ValueStorage{elem_eval.column->getValue(i)});
            } else {
                lv.elements.push_back(ValueStorage{Value{}});
            }
        }
        col.setValue(i, Value(std::move(lv)));
    }
    return {&col, true};
}

} // namespace compute
} // namespace eugraph
