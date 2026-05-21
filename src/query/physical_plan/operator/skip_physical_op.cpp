#include "query/physical_plan/operator/skip_physical_op.hpp"
#include "query/dataset/data_chunk.hpp"

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> SkipPhysicalOp::executeChunk() {
    auto child_gen = child_->executeChunk();
    int64_t remaining = skip_;

    while (auto chunk = co_await child_gen.next()) {
        size_t n = chunk->numRows();
        if (remaining >= static_cast<int64_t>(n)) {
            remaining -= static_cast<int64_t>(n);
            continue;
        }

        if (remaining > 0) {
            size_t offset = static_cast<size_t>(remaining);
            size_t new_count = n - offset;
            SelectionVector new_sel;
            new_sel.indices.resize(new_count);
            new_sel.count = new_count;
            new_sel.is_identity = false;
            for (size_t i = 0; i < new_count; ++i) {
                new_sel[i] = static_cast<uint32_t>(offset + i);
            }

            DataChunk output;
            for (auto& col : chunk->columns) {
                if ((col.form == VectorForm::FLAT || col.form == VectorForm::DICTIONARY) && col.buffer) {
                    SelectionVector mapped = new_sel;
                    if (col.form == VectorForm::DICTIONARY) {
                        for (size_t i = 0; i < new_count; ++i) {
                            mapped[i] = col.dict_sel[new_sel[i]];
                        }
                    }
                    output.columns.push_back(Column::dict(col.buffer, mapped));
                } else if (col.form == VectorForm::CONSTANT) {
                    output.columns.push_back(Column::constant(col.constant_value));
                } else {
                    output.columns.push_back(Column(col.type));
                }
            }
            output.count = new_count;
            co_yield std::move(output);
            remaining = 0;

            // Pass through remaining chunks unchanged
            while (true) {
                auto rest = co_await child_gen.next();
                if (!rest.has_value())
                    break;
                co_yield std::move(*rest);
            }
            co_return;
        } else {
            co_yield std::move(*chunk);
        }
    }
}

} // namespace compute
} // namespace eugraph
