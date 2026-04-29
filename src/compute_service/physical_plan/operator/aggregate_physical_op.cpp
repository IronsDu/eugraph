#include "compute_service/physical_plan/operator/aggregate_physical_op.hpp"
#include "compute_service/executor/row.hpp"
#include "compute_service/parser/ast.hpp"

#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<RowBatch> AggregatePhysicalOp::execute() {
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

    auto child_gen = child_->execute();
    while (auto batch = co_await child_gen.next()) {
        for (const auto& row : batch->rows) {
            Row key;
            for (const auto& gk : group_keys_) {
                Value v = evaluator_.evaluate(gk.expr, row, input_schema_);
                key.push_back(std::move(v));
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

                bool is_count_star =
                    (agg.func_name == "count" && std::holds_alternative<std::unique_ptr<cypher::Literal>>(agg.arg) &&
                     std::holds_alternative<cypher::NullValue>(
                         std::get<std::unique_ptr<cypher::Literal>>(agg.arg)->value));

                double val = 0;
                if (!is_count_star) {
                    Value v = evaluator_.evaluate(agg.arg, row, input_schema_);
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

    RowBatch output;
    for (const auto& key : group_order) {
        auto& state = groups.at(key);
        Row out_row;

        for (const auto& v : key) {
            out_row.push_back(v);
        }

        for (size_t i = 0; i < aggregates_.size(); ++i) {
            const auto& agg = aggregates_[i];
            auto& as = state.aggs[i];
            if (agg.func_name == "count") {
                out_row.push_back(Value(static_cast<int64_t>(as.count)));
            } else if (agg.func_name == "sum") {
                out_row.push_back(Value(static_cast<int64_t>(static_cast<int64_t>(as.sum))));
            } else if (agg.func_name == "avg") {
                double avg = (as.count > 0) ? (as.sum / static_cast<double>(as.count)) : 0.0;
                out_row.push_back(Value(avg));
            } else if (agg.func_name == "min") {
                if (as.min_set) {
                    out_row.push_back(Value(static_cast<int64_t>(static_cast<int64_t>(as.min_val))));
                } else {
                    out_row.push_back(Value{});
                }
            } else if (agg.func_name == "max") {
                if (as.max_set) {
                    out_row.push_back(Value(static_cast<int64_t>(static_cast<int64_t>(as.max_val))));
                } else {
                    out_row.push_back(Value{});
                }
            }
        }

        output.push_back(std::move(out_row));
        if (output.size() >= RowBatch::CAPACITY) {
            co_yield std::move(output);
            output.clear();
        }
    }

    if (group_keys_.empty() && group_order.empty() && has_aggregates) {
        Row out_row;
        for (size_t i = 0; i < aggregates_.size(); ++i) {
            const auto& agg = aggregates_[i];
            if (agg.func_name == "count") {
                out_row.push_back(Value(static_cast<int64_t>(0)));
            } else {
                out_row.push_back(Value{});
            }
        }
        output.push_back(std::move(out_row));
    }

    if (!output.empty()) {
        co_yield std::move(output);
    }
}

} // namespace compute
} // namespace eugraph
