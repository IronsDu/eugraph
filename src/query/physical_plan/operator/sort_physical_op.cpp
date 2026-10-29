#include "query/physical_plan/operator/sort_physical_op.hpp"
#include "common/types/temporal_value.hpp"
#include "query/dataset/row.hpp"
#include "query/evaluator/columnar_kernels.hpp"
#include "query/evaluator/expression_evaluator.hpp"

#include <algorithm>
#include <numeric>

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> SortPhysicalOp::executeChunk() {
    // Phase 1: drain all child chunks, pre-compute sort keys.
    //
    // Full sort path: materialize Rows with keys appended (payload columns in
    // [0, num_cols), keys after), then sort an index array.
    //
    // Top-N path (LIMIT pushed down): keep the child chunks and only sort
    // (chunk, row) references by their pre-computed key columns. The payload
    // is read back only for the N surviving rows. This avoids copying the
    // wide row (e.g. LDBC c9's per-row `friends` list) for every row — the
    // dominant cost when input arrives in ascending order and every candidate
    // replaces the heap worst.
    const size_t n_keys = sort_items_.size();
    const std::optional<size_t> top_n =
        (limit_hint_.has_value() && *limit_hint_ > 0) ? limit_hint_ : std::optional<size_t>{};

    size_t num_cols = 0;
    auto child_gen = cancellable(child_->executeChunkTraced());
    ExpressionEvaluator evaluator(eval_ctx_);

    std::vector<Row> all_rows;
    std::vector<DataChunk> chunks;
    std::vector<std::vector<Column>> chunk_keys;
    std::vector<std::pair<size_t, size_t>> refs;

    auto rowLess = [&](const Row& a, const Row& b) {
        for (size_t i = 0; i < n_keys; ++i) {
            const Value& va = a[num_cols + i];
            const Value& vb = b[num_cols + i];
            int cmp = cypherCompareValues(va, vb);
            if (cmp == 0)
                continue;
            return sort_items_[i].ascending ? (cmp < 0) : (cmp > 0);
        }
        return false;
    };

    while (auto chunk = co_await child_gen.next()) {
        size_t n = chunk->numRows();
        if (n == 0)
            continue;
        num_cols = chunk->numColumns();

        std::vector<Column> key_cols(n_keys);
        for (size_t k = 0; k < n_keys; ++k) {
            key_cols[k] = Column::flat(binder::BoundTypeKind::ANY, n);
            evaluator.evaluate(sort_items_[k].expr, *chunk, key_cols[k]);
        }

        if (!top_n) {
            for (size_t r = 0; r < n; ++r) {
                Row row;
                row.reserve(num_cols + n_keys);
                for (size_t c = 0; c < num_cols; ++c)
                    row.push_back(chunk->getValue(c, r));
                for (size_t k = 0; k < n_keys; ++k)
                    row.push_back(key_cols[k].getValue(r));
                all_rows.push_back(std::move(row));
            }
            continue;
        }

        const size_t ci = chunks.size();
        chunks.push_back(std::move(*chunk));
        chunk_keys.push_back(std::move(key_cols));
        for (size_t r = 0; r < n; ++r)
            refs.emplace_back(ci, r);
    }

    if (top_n) {
        if (refs.empty())
            co_return;
        auto refLess = [&](const std::pair<size_t, size_t>& a, const std::pair<size_t, size_t>& b) {
            for (size_t i = 0; i < n_keys; ++i) {
                const Value& va = chunk_keys[a.first][i].getValue(a.second);
                const Value& vb = chunk_keys[b.first][i].getValue(b.second);
                int cmp = cypherCompareValues(va, vb);
                if (cmp == 0)
                    continue;
                return sort_items_[i].ascending ? (cmp < 0) : (cmp > 0);
            }
            return false;
        };
        using Diff = std::vector<std::pair<size_t, size_t>>::difference_type;
        const size_t k = std::min(*top_n, refs.size());
        std::partial_sort(refs.begin(), refs.begin() + static_cast<Diff>(k), refs.end(), refLess);
        refs.resize(k);

        DataChunk output;
        for (size_t c = 0; c < num_cols; ++c)
            output.columns.push_back(Column::flat(binder::BoundTypeKind::ANY, DataChunk::DEFAULT_CAPACITY));
        size_t row_idx = 0;
        for (const auto& [ci, ri] : refs) {
            for (size_t c = 0; c < num_cols; ++c)
                output.columns[c].setValue(row_idx, chunks[ci].getValue(c, ri));
            ++row_idx;
            ++output.count;
            if (row_idx >= DataChunk::DEFAULT_CAPACITY) {
                co_yield std::move(output);
                output = DataChunk();
                for (size_t c = 0; c < num_cols; ++c)
                    output.columns.push_back(Column::flat(binder::BoundTypeKind::ANY, DataChunk::DEFAULT_CAPACITY));
                row_idx = 0;
            }
        }
        if (output.count > 0)
            co_yield std::move(output);
        co_return;
    }

    if (all_rows.empty())
        co_return;

    std::vector<size_t> indices(all_rows.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) { return rowLess(all_rows[a], all_rows[b]); });

    DataChunk output;
    for (size_t c = 0; c < num_cols; ++c)
        output.columns.push_back(Column::flat(binder::BoundTypeKind::ANY, DataChunk::DEFAULT_CAPACITY));

    size_t row_idx = 0;
    for (size_t sorted_i : indices) {
        const Row& row = all_rows[sorted_i];
        for (size_t c = 0; c < num_cols && c < row.size(); ++c)
            output.columns[c].setValue(row_idx, row[c]);
        ++row_idx;
        ++output.count;
        if (row_idx >= DataChunk::DEFAULT_CAPACITY) {
            co_yield std::move(output);
            output = DataChunk();
            for (size_t c = 0; c < num_cols; ++c)
                output.columns.push_back(Column::flat(binder::BoundTypeKind::ANY, DataChunk::DEFAULT_CAPACITY));
            row_idx = 0;
        }
    }
    if (output.count > 0)
        co_yield std::move(output);
}

} // namespace compute
} // namespace eugraph
