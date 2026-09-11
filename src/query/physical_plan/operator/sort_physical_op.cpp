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
    // Phase 1: drain all child chunks, materialize rows + pre-compute sort keys.
    //
    // The pre-computed keys are stored in the SAME Row as the payload columns
    // rather than in a parallel vector<vector<Value>>: one contiguous
    // allocation per row instead of two, and the comparator below indexes the
    // keys directly. Payload columns live in [0, num_cols); keys follow.
    std::vector<Row> all_rows;
    size_t num_cols = 0;
    const size_t n_keys = sort_items_.size();

    auto child_gen = child_->executeChunk();
    ExpressionEvaluator evaluator(eval_ctx_);

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

        for (size_t r = 0; r < n; ++r) {
            Row row;
            row.reserve(num_cols + n_keys);
            for (size_t c = 0; c < num_cols; ++c) {
                row.push_back(chunk->getValue(c, r));
            }
            for (size_t k = 0; k < n_keys; ++k) {
                row.push_back(key_cols[k].getValue(r));
            }
            all_rows.push_back(std::move(row));
        }
    }

    if (all_rows.empty())
        co_return;

    // Phase 2: sort by index array using pre-computed keys.
    std::vector<size_t> indices(all_rows.size());
    std::iota(indices.begin(), indices.end(), 0);

    std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
        for (size_t i = 0; i < n_keys; ++i) {
            const Value& va = all_rows[a][num_cols + i];
            const Value& vb = all_rows[b][num_cols + i];
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
