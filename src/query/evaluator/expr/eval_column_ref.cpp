#include "query/evaluator/vectorized_evaluator.hpp"

#include <spdlog/spdlog.h>

namespace eugraph {
namespace compute {

VectorizedEvaluator::EvalResult VectorizedEvaluator::evalColumnRef(const binder::BoundColumnRef& ref,
                                                                   const DataChunk& input) {
    if (ref.column_index < input.columns.size()) {
        return {&input.columns[ref.column_index], false};
    }
    spdlog::warn("BoundColumnRef name='{}' idx={} but input has {} columns", ref.name, ref.column_index,
                 input.columns.size());
    auto& col = acquireTempColumn(binder::BoundTypeKind::ANY, input.numRows());
    return {&col, true};
}

} // namespace compute
} // namespace eugraph
