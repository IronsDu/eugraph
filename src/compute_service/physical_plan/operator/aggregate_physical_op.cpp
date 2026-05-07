#include "compute_service/physical_plan/operator/aggregate_physical_op.hpp"
#include "compute_service/executor/row.hpp"
#include "compute_service/executor/vectorized_evaluator.hpp"

#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> AggregatePhysicalOp::executeChunk() {
    struct AggState {
        int64_t count = 0;
        double sum = 0;
        double min_val = std::numeric_limits<double>::max();
        double max_val = std::numeric_limits<double>::lowest();
        bool min_set = false;
        bool max_set = false;
        std::unordered_set<Value, ValueHash> distinct_values;
    };

    struct GroupState {
        int64_t row_count = 0;
        std::vector<AggState> aggs;
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
                gs.aggs.resize(aggregates_.size());
                group_order.push_back(key);
                it = groups.emplace(std::move(key), std::move(gs)).first;
            }

            auto& state = it->second;
            state.row_count++;

            for (size_t i = 0; i < aggregates_.size(); ++i) {
                const auto& agg = aggregates_[i];
                auto& as = state.aggs[i];

                bool is_count_star = (agg.func_name == "count" &&
                                      std::holds_alternative<binder::BoundLiteral>(agg.arg) &&
                                      isNull(std::get<binder::BoundLiteral>(agg.arg).value));

                double val = 0;
                if (!is_count_star) {
                    Value v = std::move(chunk_agg_vals[i][r]);
                    if (std::holds_alternative<std::monostate>(v)) {
                        continue;
                    }
                    if (std::holds_alternative<int64_t>(v)) {
                        val = static_cast<double>(std::get<int64_t>(v));
                    } else if (std::holds_alternative<double>(v)) {
                        val = std::get<double>(v);
                    } else {
                        if (agg.func_name == "count") {
                            if (agg.distinct) {
                                if (!as.distinct_values.insert(v).second) {
                                    continue;
                                }
                            }
                            as.count++;
                        }
                        continue;
                    }

                    if (agg.distinct) {
                        if (!as.distinct_values.insert(Value(static_cast<int64_t>(val))).second) {
                            continue;
                        }
                    }
                }

                if (agg.func_name == "count") {
                    as.count++;
                } else if (agg.func_name == "sum") {
                    as.sum += val;
                } else if (agg.func_name == "avg") {
                    as.sum += val;
                    as.count++;
                } else if (agg.func_name == "min") {
                    if (!as.min_set || val < as.min_val) {
                        as.min_val = val;
                        as.min_set = true;
                    }
                } else if (agg.func_name == "max") {
                    if (!as.max_set || val > as.max_val) {
                        as.max_val = val;
                        as.max_set = true;
                    }
                }
            }
        }
    }

    // Build output FLAT DataChunk.
    size_t out_cols = group_keys_.size() + aggregates_.size();

    auto buildOutputChunk = [&]() -> DataChunk {
        DataChunk dc;
        for (size_t c = 0; c < out_cols; ++c) {
            dc.columns.push_back(Column::flat(binder::BoundTypeKind::ANY, DataChunk::DEFAULT_CAPACITY));
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
            auto& as = state.aggs[i];
            size_t col_idx = group_keys_.size() + i;

            if (agg.func_name == "count") {
                output.columns[col_idx].setValue(row_idx, Value(static_cast<int64_t>(as.count)));
            } else if (agg.func_name == "sum") {
                output.columns[col_idx].setValue(row_idx, Value(static_cast<int64_t>(as.sum)));
            } else if (agg.func_name == "avg") {
                double avg = (as.count > 0) ? (as.sum / static_cast<double>(as.count)) : 0.0;
                output.columns[col_idx].setValue(row_idx, Value(avg));
            } else if (agg.func_name == "min") {
                if (as.min_set) {
                    output.columns[col_idx].setValue(row_idx, Value(static_cast<int64_t>(as.min_val)));
                } else {
                    output.columns[col_idx].setNull(row_idx);
                }
            } else if (agg.func_name == "max") {
                if (as.max_set) {
                    output.columns[col_idx].setValue(row_idx, Value(static_cast<int64_t>(as.max_val)));
                } else {
                    output.columns[col_idx].setNull(row_idx);
                }
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
            if (agg.func_name == "count") {
                output.columns[i].setValue(0, Value(static_cast<int64_t>(0)));
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
