#include "query/evaluator/expression_evaluator.hpp"

#include <spdlog/spdlog.h>

namespace eugraph {
namespace compute {

ExpressionEvaluator::EvalResult ExpressionEvaluator::evalColumnRef(const binder::BoundColumnRef& ref,
                                                                   const DataChunk& input) {
    uint32_t idx = ref.column_index;
    if (ref.slot_id != 0) {
        if (auto it = temp_column_overrides_.find(ref.slot_id); it != temp_column_overrides_.end())
            idx = it->second;
    }
    if (idx < input.columns.size()) {
        return {&input.columns[idx], false};
    }
    spdlog::warn("BoundColumnRef name='{}' idx={} but input has {} columns", ref.name, idx, input.columns.size());
    auto& col = acquireTempColumn(binder::BoundTypeKind::ANY, input.numRows());
    return {&col, true};
}

} // namespace compute
} // namespace eugraph
