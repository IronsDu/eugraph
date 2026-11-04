#include "query/physical_plan/operator/semi_join_physical_op.hpp"

#include "query/dataset/cell_access.hpp"

#include <algorithm>
#include <cstdlib>
#include <span>
#include <unordered_set>

namespace eugraph {
namespace compute {

namespace {

VertexId sjVertexId(const Value& v) {
    if (std::holds_alternative<VertexRef>(v))
        return std::get<VertexRef>(v).id;
    if (std::holds_alternative<VertexValuePtr>(v)) {
        const auto& p = std::get<VertexValuePtr>(v);
        return p ? p->id : INVALID_VERTEX_ID;
    }
    return INVALID_VERTEX_ID;
}

bool sjVertexLike(binder::BoundTypeKind kind) {
    return kind == binder::BoundTypeKind::VERTEX || kind == binder::BoundTypeKind::VERTEX_REF;
}

} // namespace

folly::coro::AsyncGenerator<DataChunk> SemiJoinPhysicalOp::executePerRow() {
    auto left_gen = cancellable(left_->executeChunkTraced());

    while (auto left_chunk = co_await left_gen.next()) {
        if (!left_chunk || left_chunk->count == 0)
            continue;

        size_t n_left = left_chunk->count;
        std::vector<bool> matched(n_left, false);

        for (size_t i = 0; i < n_left; ++i) {
            std::vector<Value> corr_values;
            corr_values.reserve(left_correlation_cols_.size());
            for (uint32_t col_idx : left_correlation_cols_) {
                corr_values.push_back(left_chunk->getValue(col_idx, i));
            }
            correlated_source_->setValues(std::move(corr_values));

            auto right_gen = cancellable(right_->executeChunkTraced());
            auto right_chunk = co_await right_gen.next();
            size_t right_rows = right_chunk ? right_chunk->numRows() : 0;
            if (right_chunk && right_rows > 0) {
                matched[i] = true;
            }
        }

        // Build filtered output with only matched rows
        SelectionVector filtered;
        filtered.is_identity = false;
        filtered.indices.reserve(n_left);
        for (size_t i = 0; i < n_left; ++i) {
            bool keep = anti_ ? !matched[i] : matched[i];
            if (keep)
                filtered.indices.push_back(static_cast<uint32_t>(i));
        }
        filtered.count = filtered.indices.size();

        if (filtered.count == 0)
            continue;

        DataChunk output;
        output.columns.reserve(left_chunk->columns.size());
        for (auto& col : left_chunk->columns) {
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

folly::coro::AsyncGenerator<DataChunk> SemiJoinPhysicalOp::executeChunk() {
    const char* env = std::getenv("EUGRAPH_SJ_BATCH");
    const size_t batch_limit = env ? std::strtoul(env, nullptr, 10) : 512;
    bool can_batch = batch_limit > 0 && correlated_source_ && !right_correlation_cols_.empty() &&
                     right_correlation_cols_.size() == left_correlation_cols_.size() &&
                     right_corr_types_.size() == right_correlation_cols_.size();
    for (const auto& t : right_corr_types_) {
        if (!sjVertexLike(t.kind)) {
            can_batch = false;
            break;
        }
    }
    if (!can_batch) {
        auto gen = executePerRow();
        while (auto chunk = co_await gen.next())
            co_yield std::move(*chunk);
        co_return;
    }

    std::vector<Row> batch;
    batch.reserve(batch_limit);
    std::vector<binder::BoundTypeKind> left_kinds;
    auto left_gen = cancellable(left_->executeChunkTraced());
    while (auto left_chunk = co_await left_gen.next()) {
        if (left_chunk->count == 0)
            continue;
        if (left_kinds.empty())
            for (const auto& col : left_chunk->columns)
                left_kinds.push_back(col.type);
        for (size_t r = 0; r < left_chunk->count; ++r) {
            Row row;
            row.reserve(left_chunk->columns.size());
            for (size_t c = 0; c < left_chunk->columns.size(); ++c)
                row.push_back(left_chunk->getValue(c, r));
            batch.push_back(std::move(row));
            if (batch.size() >= batch_limit) {
                auto gen = executeBatch(std::move(batch), left_kinds);
                while (auto chunk = co_await gen.next())
                    co_yield std::move(*chunk);
                batch.clear();
            }
        }
    }
    if (!batch.empty()) {
        auto gen = executeBatch(std::move(batch), left_kinds);
        while (auto chunk = co_await gen.next())
            co_yield std::move(*chunk);
    }
}

folly::coro::AsyncGenerator<DataChunk> SemiJoinPhysicalOp::executeBatch(std::vector<Row> left_rows,
                                                                        std::vector<binder::BoundTypeKind> left_kinds) {
    std::vector<std::vector<Value>> corr_rows;
    corr_rows.reserve(left_rows.size());
    for (const auto& row : left_rows) {
        std::vector<Value> values;
        values.reserve(left_correlation_cols_.size());
        for (uint32_t col : left_correlation_cols_)
            values.push_back(row[col]);
        corr_rows.push_back(std::move(values));
    }
    correlated_source_->setRows(std::move(corr_rows));

    const size_t corr_n = right_correlation_cols_.size();

    // Flatten the right-side correlation keys instead of allocating one
    // vector<VertexId> per right row. The spans below point into right_flat and
    // are built only after the generator is exhausted, so reallocation cannot
    // invalidate them.
    std::vector<VertexId> right_flat;
    auto right_gen = cancellable(right_->executeChunkTraced());
    while (auto right_chunk = co_await right_gen.next()) {
        for (size_t r = 0; r < right_chunk->count; ++r) {
            const size_t base = right_flat.size();
            bool valid = true;
            for (uint32_t c : right_correlation_cols_) {
                const VertexId vid = columnVertexId(right_chunk->columns[c], r);
                if (vid == INVALID_VERTEX_ID) {
                    valid = false;
                    break;
                }
                right_flat.push_back(vid);
            }
            if (!valid)
                right_flat.resize(base);
        }
    }

    std::vector<std::span<const VertexId>> right_keys;
    right_keys.reserve(corr_n == 0 ? 0 : right_flat.size() / corr_n);
    for (size_t off = 0; off + corr_n <= right_flat.size(); off += corr_n)
        right_keys.emplace_back(right_flat.data() + off, corr_n);

    auto key_less = [](std::span<const VertexId> a, std::span<const VertexId> b) {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
    };
    std::sort(right_keys.begin(), right_keys.end(), key_less);
    right_keys.erase(std::unique(right_keys.begin(), right_keys.end(),
                                 [](std::span<const VertexId> a, std::span<const VertexId> b) {
                                     return std::equal(a.begin(), a.end(), b.begin(), b.end());
                                 }),
                     right_keys.end());

    DataChunk out;
    out.columns.reserve(left_kinds.size());
    for (auto kind : left_kinds)
        out.columns.push_back(Column::flat(kind));
    out.reserve(left_rows.size());
    std::vector<VertexId> left_key(corr_n, INVALID_VERTEX_ID);
    for (const auto& row : left_rows) {
        bool valid = true;
        for (size_t i = 0; i < corr_n; ++i) {
            left_key[i] = sjVertexId(row[left_correlation_cols_[i]]);
            if (left_key[i] == INVALID_VERTEX_ID) {
                valid = false;
                break;
            }
        }
        const std::span<const VertexId> key(left_key.data(), corr_n);
        bool present = valid && corr_n != 0 && std::binary_search(right_keys.begin(), right_keys.end(), key, key_less);
        if (anti_ ? present : !present)
            continue;
        for (size_t c = 0; c < left_kinds.size(); ++c)
            out.columns[c].setValue(out.count, row[c]);
        ++out.count;
    }
    if (out.count > 0) {
        out.sel = SelectionVector::identity(out.count);
        co_yield std::move(out);
    }
}

} // namespace compute
} // namespace eugraph
