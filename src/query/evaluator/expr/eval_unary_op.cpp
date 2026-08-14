#include "query/evaluator/vectorized_evaluator.hpp"

namespace eugraph {
namespace compute {

void VectorizedEvaluator::evalUnaryOp(const binder::BoundUnaryOp& op, const DataChunk& input, Column& result,
                                      size_t count) {
    auto operand = evaluateInternal(op.operand, input);
    if (!operand.column)
        return;

    if (op.batch_fn) {
        op.batch_fn(*operand.column, result, count);
    }
}

} // namespace compute
} // namespace eugraph
