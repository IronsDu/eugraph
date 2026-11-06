#include "query/physical_plan/operator/path_count_physical_op.hpp"

#include "query/dataset/cell_access.hpp"
#include "query/dataset/data_chunk.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace eugraph {
namespace compute {

namespace {

constexpr size_t kDefaultPathCountCacheEntries = 1U << 20U;

/// Per-operator bound. The cache is an optimisation only; when it is full
/// degrees are recomputed instead of growing without limit on large graphs.
size_t pathCountCacheLimit() {
    const char* env = std::getenv("EUGRAPH_PATH_COUNT_CACHE_MAX_ENTRIES");
    if (env == nullptr || *env == '\0')
        return kDefaultPathCountCacheEntries;
    char* end = nullptr;
    const unsigned long long value = std::strtoull(env, &end, 10);
    if (end == env || *end != '\0')
        return kDefaultPathCountCacheEntries;
    return static_cast<size_t>(value);
}

VertexId edgeSource(const Value& value) {
    if (const auto* key = std::get_if<EdgeKey>(&value))
        return key->src_id;
    if (const auto* edge = std::get_if<EdgeValuePtr>(&value))
        return (*edge)->src_id;
    return INVALID_VERTEX_ID;
}

} // namespace

folly::coro::AsyncGenerator<DataChunk> PathCountPhysicalOp::executeChunk() {
    auto child_gen = cancellable(child_->executeChunkTraced());
    uint64_t total = 0;
    const size_t cache_limit = pathCountCacheLimit();
    std::unordered_map<VertexId, uint64_t> degree_cache;
    std::vector<ISyncGraphDataStore::EdgeIndexEntry> scratch;

    while (auto chunk = co_await child_gen.next()) {
        const size_t rows = chunk->numRows();
        const Column& source_col = chunk->columns[source_column_];
        for (size_t row = 0; row < rows; ++row) {
            const VertexId source = columnVertexId(source_col, row);
            if (source == INVALID_VERTEX_ID)
                continue;

            auto cached = degree_cache.find(source);
            uint64_t computed_degree = 0;
            if (cached == degree_cache.end()) {
                auto gen = cancellable(store_.scanEdges(source, Direction::OUT, std::nullopt, scratch));
                while (auto batch = co_await gen.next()) {
                    for (const auto& entry : *batch) {
                        if (!edge_labels_.empty() && std::find(edge_labels_.begin(), edge_labels_.end(),
                                                               entry.edge_label_id) == edge_labels_.end())
                            continue;
                        ++computed_degree;
                    }
                }
                if (degree_cache.size() < cache_limit)
                    cached = degree_cache.emplace(source, computed_degree).first;
            }

            uint64_t choices = (cached != degree_cache.end()) ? cached->second : computed_degree;
            for (uint32_t col : previous_edge_columns_) {
                if (col >= chunk->columns.size())
                    continue;
                if (edgeSource(chunk->columns[col].getValue(row)) == source && choices > 0)
                    --choices;
            }
            total += choices;
        }
    }

    DataChunk output;
    output.count = 1;
    output.addColumn(binder::BoundTypeKind::INT64);
    output.columns[0].setInt64(0, static_cast<int64_t>(total));
    co_yield std::move(output);
}

} // namespace compute
} // namespace eugraph
