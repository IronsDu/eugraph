#include "query/physical_plan/physical_operator_base.hpp"

namespace eugraph {
namespace compute {

// ==================== Default executeChunk (bridge from execute) ====================

folly::coro::AsyncGenerator<DataChunk> PhysicalOperator::executeChunk() {
    auto gen = execute();
    while (auto batch = co_await gen.next()) {
        co_yield rowBatchToDataChunk(*batch);
    }
}

// ==================== executeViaChunk (bridge from executeChunk to execute) ====================

folly::coro::AsyncGenerator<RowBatch> PhysicalOperator::executeViaChunk() {
    auto gen = executeChunk();
    while (auto chunk = co_await gen.next()) {
        co_yield dataChunkToRowBatch(*chunk);
    }
}

// ==================== Conversion utilities ====================

DataChunk rowBatchToDataChunk(const RowBatch& batch) {
    DataChunk dc;
    if (batch.rows.empty())
        return dc;

    size_t num_cols = batch.rows[0].size();
    for (size_t c = 0; c < num_cols; ++c) {
        dc.columns.push_back(Column::flat(binder::BoundTypeKind::ANY, batch.rows.size()));
    }

    for (size_t r = 0; r < batch.rows.size(); ++r) {
        for (size_t c = 0; c < num_cols && c < batch.rows[r].size(); ++c) {
            dc.columns[c].setValue(r, batch.rows[r][c]);
        }
    }
    dc.count = batch.rows.size();
    return dc;
}

RowBatch dataChunkToRowBatch(const DataChunk& chunk) {
    RowBatch rb;
    auto rows = chunk.toRows();
    for (auto& row : rows) {
        rb.push_back(std::move(row));
    }
    return rb;
}

folly::coro::AsyncGenerator<DataChunk> wrapRowBatchToChunkGenerator(folly::coro::AsyncGenerator<RowBatch> gen) {
    while (auto batch = co_await gen.next()) {
        co_yield rowBatchToDataChunk(*batch);
    }
}

} // namespace compute
} // namespace eugraph
