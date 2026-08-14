#include "query/evaluator/vectorized_evaluator.hpp"

#include "query/planner/bound_expression/bound_label_cast.hpp"

namespace eugraph {
namespace compute {

VectorizedEvaluator::EvalResult VectorizedEvaluator::evalLabelCast(const binder::BoundLabelCast& cast,
                                                                   const DataChunk& input) {
    size_t count = input.numRows();
    auto inner = evaluateInternal(cast.object, input);
    auto& col = acquireTempColumn(binder::BoundTypeKind::VERTEX, count);
    if (inner.column) {
        for (size_t i = 0; i < count; ++i) {
            col.setValue(i, inner.column->getValue(i));
        }
    }
    return {&col, true};
}

} // namespace compute
} // namespace eugraph
