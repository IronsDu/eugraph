#include "query/physical_plan/operator/sort_physical_op.hpp"
#include "common/types/temporal_value.hpp"
#include "query/dataset/row.hpp"
#include "query/evaluator/vectorized_evaluator.hpp"

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

            bool less = false, greater = false;
            std::visit(
                [&less, &greater](const auto& la, const auto& lb) {
                    using A = std::decay_t<decltype(la)>;
                    using B = std::decay_t<decltype(lb)>;
                    // NULL sorts after all non-NULL values (ascending).
                    // When both are NULL they compare equal.
                    if constexpr (std::is_same_v<A, std::monostate> || std::is_same_v<B, std::monostate>) {
                        if constexpr (std::is_same_v<A, std::monostate> && std::is_same_v<B, std::monostate>) {
                            less = false;
                            greater = false;
                        } else if constexpr (std::is_same_v<A, std::monostate>) {
                            // la is NULL, lb is not → NULL > non-NULL → la > lb
                            greater = true;
                        } else {
                            // lb is NULL → lb > la → la < lb
                            less = true;
                        }
                    } else if constexpr ((std::is_same_v<A, int64_t> && std::is_same_v<B, int64_t>) ||
                                         (std::is_same_v<A, double> && std::is_same_v<B, double>)) {
                        less = la < lb;
                        greater = la > lb;
                    } else if constexpr (std::is_same_v<A, std::string> && std::is_same_v<B, std::string>) {
                        less = la < lb;
                        greater = la > lb;
                    } else if constexpr (std::is_same_v<A, DateTimeValue> && std::is_same_v<B, DateTimeValue>) {
                        if (la.kind == lb.kind) {
                            less = temporalLess(la, lb);
                            greater = temporalLess(lb, la);
                        }
                    } else if constexpr (std::is_same_v<A, TimeValue> && std::is_same_v<B, TimeValue>) {
                        if (la.kind == lb.kind) {
                            less = temporalLess(la, lb);
                            greater = temporalLess(lb, la);
                        }
                    }
                },
                va, vb);

            if (!sort_items_[i].ascending) {
                std::swap(less, greater);
            }
            if (less)
                return true;
            if (greater)
                return false;
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
