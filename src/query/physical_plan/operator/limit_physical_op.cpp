#include "query/physical_plan/operator/limit_physical_op.hpp"
#include "query/executor/data_chunk.hpp"

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> LimitPhysicalOp::executeChunk() {
    auto child_gen = child_->executeChunk();
    int64_t remaining = limit_;

    while (remaining > 0) {
        auto chunk = co_await child_gen.next();
        if (!chunk.has_value())
            break;

        size_t n = chunk->numRows();
        if (static_cast<int64_t>(n) <= remaining) {
            remaining -= static_cast<int64_t>(n);
            co_yield std::move(*chunk);
        } else {
            size_t limit = static_cast<size_t>(remaining);
            SelectionVector new_sel;
            new_sel.indices.resize(limit);
            new_sel.count = limit;
            new_sel.is_identity = false;
            for (size_t i = 0; i < limit; ++i) {
                new_sel[i] = static_cast<uint32_t>(i);
            }

            DataChunk output;
            for (auto& col : chunk->columns) {
                if ((col.form == VectorForm::FLAT || col.form == VectorForm::DICTIONARY) && col.buffer) {
                    SelectionVector mapped = new_sel;
                    if (col.form == VectorForm::DICTIONARY) {
                        for (size_t i = 0; i < limit; ++i) {
                            mapped[i] = col.dict_sel[i];
                        }
                    }
                    output.columns.push_back(Column::dict(col.buffer, mapped));
                } else if (col.form == VectorForm::CONSTANT) {
                    output.columns.push_back(Column::constant(col.constant_value));
                } else {
                    output.columns.push_back(Column(col.type));
                }
            }
            output.count = limit;
            co_yield std::move(output);
            co_return;
        }
    }
}

} // namespace compute
} // namespace eugraph
