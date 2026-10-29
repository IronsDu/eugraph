#include "query/physical_plan/operator/materialize_physical_op.hpp"

namespace eugraph {
namespace compute {

namespace {

DataChunk makeEmptyChunk(const std::vector<binder::BoundType>& types) {
    DataChunk chunk;
    chunk.columns.reserve(types.size());
    for (const auto& t : types)
        chunk.columns.push_back(Column::flat(t.kind, DataChunk::DEFAULT_CAPACITY));
    return chunk;
}

} // namespace

folly::coro::AsyncGenerator<DataChunk> MaterializePhysicalOp::replay() {
    DataChunk out = makeEmptyChunk(output_types_);
    for (const auto& row : rows_) {
        for (size_t c = 0; c < row.size() && c < out.columns.size(); ++c)
            out.columns[c].setValue(out.count, row[c]);
        ++out.count;
        if (out.count >= DataChunk::DEFAULT_CAPACITY) {
            out.sel = SelectionVector::identity(out.count);
            co_yield std::move(out);
            out = makeEmptyChunk(output_types_);
        }
    }
    if (out.count > 0) {
        out.sel = SelectionVector::identity(out.count);
        co_yield std::move(out);
    }
}

folly::coro::AsyncGenerator<DataChunk> MaterializePhysicalOp::executeChunk() {
    if (cached_) {
        auto gen = replay();
        while (auto chunk = co_await gen.next())
            co_yield std::move(*chunk);
        co_return;
    }
    if (disabled_) {
        auto child_gen = cancellable(child_->executeChunkTraced());
        while (auto chunk = co_await child_gen.next())
            co_yield std::move(*chunk);
        co_return;
    }

    // First execution: drain the child once. If it is too large, drop the
    // cache and re-run the (read-only) child as a plain stream.
    auto child_gen = cancellable(child_->executeChunkTraced());
    while (auto chunk = co_await child_gen.next()) {
        if (chunk->count == 0)
            continue;
        if (rows_.size() + chunk->count > kMaxCachedRows) {
            rows_.clear();
            rows_.shrink_to_fit();
            disabled_ = true;
            break;
        }
        for (size_t r = 0; r < chunk->count; ++r) {
            Row row;
            row.reserve(chunk->columns.size());
            for (size_t c = 0; c < chunk->columns.size(); ++c)
                row.push_back(chunk->getValue(c, r));
            rows_.push_back(std::move(row));
        }
    }

    if (disabled_) {
        auto retry = cancellable(child_->executeChunkTraced());
        while (auto chunk = co_await retry.next())
            co_yield std::move(*chunk);
        co_return;
    }

    cached_ = true;
    auto gen = replay();
    while (auto chunk = co_await gen.next())
        co_yield std::move(*chunk);
}

} // namespace compute
} // namespace eugraph
