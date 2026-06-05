#include "query/physical_plan/operator/filter_physical_op.hpp"
#include <spdlog/spdlog.h>

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> FilterPhysicalOp::executeChunk() {
    auto child_gen = child_->executeChunk();

    while (auto chunk = co_await child_gen.next()) {
        size_t n = chunk->numRows();

        VectorizedEvaluator evaluator(eval_ctx_);
        std::vector<bool> predicate(n);
        evaluator.evaluatePredicate(predicate_, *chunk, predicate);

        int pass_count = 0;
        for (auto p : predicate)
            if (p)
                pass_count++;
        spdlog::info("[Filter] input_rows={} passed={}", n, pass_count);

        // Build filtered SelectionVector
        SelectionVector filtered;
        filtered.is_identity = false;
        filtered.indices.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            if (predicate[i]) {
                filtered.indices.push_back(static_cast<uint32_t>(i));
            }
        }
        filtered.count = filtered.indices.size();

        if (filtered.count == 0)
            continue;

        DataChunk output;
        output.columns.reserve(chunk->columns.size());
        for (auto& col : chunk->columns) {
            if ((col.form == VectorForm::FLAT || col.form == VectorForm::DICTIONARY) && col.buffer) {
                SelectionVector mapped;
                mapped.is_identity = false;
                mapped.indices.reserve(filtered.count);
                for (size_t i = 0; i < filtered.count; ++i) {
                    uint32_t physical = filtered[i];
                    if (col.form == VectorForm::DICTIONARY) {
                        mapped.indices.push_back(col.dict_sel[physical]);
                    } else {
                        mapped.indices.push_back(physical);
                    }
                }
                mapped.count = filtered.count;
                output.columns.push_back(Column::dict(col.buffer, mapped));
            } else if (col.form == VectorForm::CONSTANT) {
                output.columns.push_back(Column::constant(col.constant_value));
            } else {
                output.columns.push_back(Column(col.type));
            }
        }
        output.count = filtered.count;
        co_yield std::move(output);
    }
}

} // namespace compute
} // namespace eugraph
