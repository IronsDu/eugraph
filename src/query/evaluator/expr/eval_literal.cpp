#include "query/evaluator/vectorized_evaluator.hpp"

namespace eugraph {
namespace compute {

void VectorizedEvaluator::evalLiteral(const binder::BoundLiteral& lit, Column& result, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        result.setValue(i, lit.value);
    }
}

} // namespace compute
} // namespace eugraph
