#include "query/physical_plan/operator/sort_physical_op.hpp"
#include "common/types/temporal_value.hpp"
#include "query/dataset/row.hpp"
#include "query/evaluator/vectorized_evaluator.hpp"
#include "query/function/compare_ops.hpp"

#include <algorithm>
#include <numeric>

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> SortPhysicalOp::executeChunk() {
    // Phase 1: drain all child chunks, materialize rows + pre-compute sort keys.
    std::vector<Row> all_rows;
    std::vector<std::vector<Value>> all_keys;
    size_t num_cols = 0;

    auto child_gen = child_->executeChunk();
    VectorizedEvaluator evaluator(eval_ctx_);

    while (auto chunk = co_await child_gen.next()) {
        size_t n = chunk->numRows();
        if (n == 0)
            continue;
        num_cols = chunk->numColumns();

        std::vector<std::vector<Value>> chunk_key_vals(sort_items_.size());
        for (size_t k = 0; k < sort_items_.size(); ++k) {
            auto key_col = Column::flat(binder::BoundTypeKind::ANY, n);
            evaluator.evaluate(sort_items_[k].expr, *chunk, key_col);
            chunk_key_vals[k].reserve(n);
            for (size_t r = 0; r < n; ++r) {
                chunk_key_vals[k].push_back(key_col.getValue(r));
            }
        }

        for (size_t r = 0; r < n; ++r) {
            Row row;
            row.reserve(num_cols);
            for (size_t c = 0; c < num_cols; ++c) {
                row.push_back(chunk->getValue(c, r));
            }
            all_rows.push_back(std::move(row));

            std::vector<Value> keys;
            keys.reserve(sort_items_.size());
            for (size_t k = 0; k < sort_items_.size(); ++k) {
                keys.push_back(std::move(chunk_key_vals[k][r]));
            }
            all_keys.push_back(std::move(keys));
        }
    }

    if (all_rows.empty())
        co_return;

    // Phase 2: sort by index array using pre-computed keys.
    std::vector<size_t> indices(all_rows.size());
    std::iota(indices.begin(), indices.end(), 0);

    std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
        for (size_t i = 0; i < sort_items_.size(); ++i) {
            const Value& va = all_keys[a][i];
            const Value& vb = all_keys[b][i];
            int cmp = cypherCompareValues(va, vb);
            if (cmp == 0)
                continue;
            return sort_items_[i].ascending ? (cmp < 0) : (cmp > 0);
        }
        return false;
    });

    // Phase 3: yield sorted rows as FLAT DataChunks.
    DataChunk output;
    for (size_t c = 0; c < num_cols; ++c) {
        output.columns.push_back(Column::flat(binder::BoundTypeKind::ANY, DataChunk::DEFAULT_CAPACITY));
    }

    size_t row_idx = 0;
    for (size_t sorted_i : indices) {
        const Row& row = all_rows[sorted_i];
        for (size_t c = 0; c < num_cols && c < row.size(); ++c) {
            output.columns[c].setValue(row_idx, row[c]);
        }
        ++row_idx;
        ++output.count;

        if (row_idx >= DataChunk::DEFAULT_CAPACITY) {
            co_yield std::move(output);
            output = DataChunk();
            for (size_t c = 0; c < num_cols; ++c) {
                output.columns.push_back(Column::flat(binder::BoundTypeKind::ANY, DataChunk::DEFAULT_CAPACITY));
            }
            row_idx = 0;
        }
    }

    if (output.count > 0) {
        co_yield std::move(output);
    }
}

} // namespace compute
} // namespace eugraph
