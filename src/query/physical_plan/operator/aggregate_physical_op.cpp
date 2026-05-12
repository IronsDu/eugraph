#include "query/physical_plan/operator/aggregate_physical_op.hpp"
#include "query/executor/row.hpp"
#include "query/executor/vectorized_evaluator.hpp"

#include <unordered_map>
#include <unordered_set>

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> AggregatePhysicalOp::executeChunk() {
    // Per-group state: one AggStateBase* per aggregate expression.
    struct GroupState {
        std::vector<std::unique_ptr<function::AggStateBase>> agg_states;
        // Per-aggregate distinct sets (only used when aggregate.distinct == true).
        std::vector<std::unordered_set<Value, ValueHash>> distinct_sets;
    };

    std::unordered_map<Row, GroupState, RowHash, RowEqual> groups;
    std::vector<Row> group_order;

    bool has_aggregates = !aggregates_.empty();

    auto child_gen = child_->executeChunk();
    VectorizedEvaluator evaluator;

    while (auto chunk = co_await child_gen.next()) {
        size_t n = chunk->numRows();
        if (n == 0)
            continue;

        // Evaluate group key expressions vectorized.
        std::vector<std::vector<Value>> chunk_gk_vals(group_keys_.size());
        for (size_t k = 0; k < group_keys_.size(); ++k) {
            auto col = Column::flat(binder::BoundTypeKind::ANY, n);
            evaluator.evaluate(group_keys_[k].expr, *chunk, col);
            chunk_gk_vals[k].reserve(n);
            for (size_t r = 0; r < n; ++r)
                chunk_gk_vals[k].push_back(col.getValue(r));
        }

        // Evaluate aggregate argument expressions vectorized.
        std::vector<std::vector<Value>> chunk_agg_vals(aggregates_.size());
        for (size_t a = 0; a < aggregates_.size(); ++a) {
            auto col = Column::flat(binder::BoundTypeKind::ANY, n);
            evaluator.evaluate(aggregates_[a].arg, *chunk, col);
            chunk_agg_vals[a].reserve(n);
            for (size_t r = 0; r < n; ++r)
                chunk_agg_vals[a].push_back(col.getValue(r));
        }

        // Aggregate each row.
        for (size_t r = 0; r < n; ++r) {
            Row key;
            for (size_t k = 0; k < group_keys_.size(); ++k) {
                key.push_back(std::move(chunk_gk_vals[k][r]));
            }

            auto it = groups.find(key);
            if (it == groups.end()) {
                GroupState gs;
                gs.agg_states.reserve(aggregates_.size());
                gs.distinct_sets.resize(aggregates_.size());
                for (const auto& agg : aggregates_) {
                    if (agg.func_def && agg.func_def->agg_init) {
                        gs.agg_states.push_back(agg.func_def->agg_init());
                    } else {
                        gs.agg_states.push_back(nullptr);
                    }
                }
                group_order.push_back(key);
                it = groups.emplace(std::move(key), std::move(gs)).first;
            }

            auto& state = it->second;

            for (size_t i = 0; i < aggregates_.size(); ++i) {
                const auto& agg = aggregates_[i];
                if (!agg.func_def || !agg.func_def->agg_update || !state.agg_states[i])
                    continue;

                Value v = std::move(chunk_agg_vals[i][r]);

                // count(*) accepts monostate (null) literal; skip nulls for other aggregates.
                bool is_count_star =
                    (agg.func_def->has_variadic_args && std::holds_alternative<binder::BoundLiteral>(agg.arg) &&
                     isNull(std::get<binder::BoundLiteral>(agg.arg).value));
                if (!is_count_star && isNull(v))
                    continue;

                // DISTINCT dedup before update.
                if (agg.distinct) {
                    if (!state.distinct_sets[i].insert(v).second)
                        continue;
                }

                agg.func_def->agg_update(*state.agg_states[i], v);
            }
        }
    }

    // Build output FLAT DataChunk.
    size_t out_cols = group_keys_.size() + aggregates_.size();

    auto buildOutputChunk = [&]() -> DataChunk {
        DataChunk dc;
        for (size_t c = 0; c < out_cols; ++c) {
            auto kind = (c < output_type_kinds_.size()) ? output_type_kinds_[c] : binder::BoundTypeKind::ANY;
            dc.columns.push_back(Column::flat(kind, DataChunk::DEFAULT_CAPACITY));
        }
        return dc;
    };

    DataChunk output = buildOutputChunk();
    size_t row_idx = 0;

    for (const auto& key : group_order) {
        auto& state = groups.at(key);

        for (size_t c = 0; c < key.size(); ++c) {
            output.columns[c].setValue(row_idx, key[c]);
        }

        for (size_t i = 0; i < aggregates_.size(); ++i) {
            const auto& agg = aggregates_[i];
            size_t col_idx = group_keys_.size() + i;

            if (agg.func_def && agg.func_def->agg_finalize && state.agg_states[i]) {
                output.columns[col_idx].setValue(row_idx, agg.func_def->agg_finalize(*state.agg_states[i]));
            } else {
                output.columns[col_idx].setNull(row_idx);
            }
        }

        ++row_idx;
        ++output.count;

        if (row_idx >= DataChunk::DEFAULT_CAPACITY) {
            co_yield std::move(output);
            output = buildOutputChunk();
            row_idx = 0;
        }
    }

    // Empty input with aggregates but no group keys → yield one row with default values.
    if (group_keys_.empty() && group_order.empty() && has_aggregates) {
        for (size_t i = 0; i < aggregates_.size(); ++i) {
            const auto& agg = aggregates_[i];
            if (agg.func_def && agg.func_def->agg_init && agg.func_def->agg_finalize) {
                auto init_state = agg.func_def->agg_init();
                output.columns[i].setValue(0, agg.func_def->agg_finalize(*init_state));
            } else {
                output.columns[i].setNull(0);
            }
        }
        output.count = 1;
    }

    if (output.count > 0) {
        co_yield std::move(output);
    }
}

} // namespace compute
} // namespace eugraph
