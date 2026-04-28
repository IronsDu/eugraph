#include "compute_service/physical_plan/physical_operator.hpp"
#include "common/types/constants.hpp"

#include <algorithm>
#include <limits>

namespace eugraph {
namespace compute {

// ==================== AllNodeScan ====================

folly::coro::AsyncGenerator<RowBatch> AllNodeScanPhysicalOp::execute() {
    // Scan all known labels
    for (const auto& [name, label_id] : label_map_) {
        auto gen = store_.scanVerticesByLabel(label_id);
        while (auto batch = co_await gen.next()) {
            RowBatch output;
            for (VertexId vid : *batch) {
                // Load properties for this vertex
                auto props = co_await store_.getVertexProperties(vid, label_id);
                VertexValue vv;
                vv.id = vid;
                vv.labels = LabelIdSet{label_id};
                vv.properties = std::move(props);
                Row row;
                row.push_back(Value(std::move(vv)));
                output.push_back(std::move(row));
            }
            if (!output.empty()) {
                co_yield std::move(output);
            }
        }
    }
}

// ==================== LabelScan ====================

folly::coro::AsyncGenerator<RowBatch> LabelScanPhysicalOp::execute() {
    auto gen = store_.scanVerticesByLabel(label_id_);
    while (auto batch = co_await gen.next()) {
        RowBatch output;
        for (VertexId vid : *batch) {
            auto props = co_await store_.getVertexProperties(vid, label_id_);
            VertexValue vv;
            vv.id = vid;
            vv.labels = LabelIdSet{label_id_};
            vv.properties = std::move(props);
            Row row;
            row.push_back(Value(std::move(vv)));
            output.push_back(std::move(row));
        }
        if (!output.empty()) {
            co_yield std::move(output);
        }
    }
}

// ==================== IndexScan ====================

folly::coro::AsyncGenerator<RowBatch> IndexScanPhysicalOp::execute() {
    folly::coro::AsyncGenerator<std::vector<VertexId>> gen;
    if (mode_ == ScanMode::EQUALITY) {
        gen = store_.scanVerticesByIndex(label_id_, prop_id_, search_value_);
    } else {
        gen = store_.scanVerticesByIndexRange(label_id_, prop_id_, search_value_, range_end_);
    }

    while (auto batch = co_await gen.next()) {
        RowBatch output;
        for (VertexId vid : *batch) {
            auto props = co_await store_.getVertexProperties(vid, label_id_);
            VertexValue vv;
            vv.id = vid;
            vv.labels = LabelIdSet{label_id_};
            vv.properties = std::move(props);
            Row row;
            row.push_back(Value(std::move(vv)));
            output.push_back(std::move(row));
        }
        if (!output.empty()) {
            co_yield std::move(output);
        }
    }
}

// ==================== Expand ====================

folly::coro::AsyncGenerator<RowBatch> ExpandPhysicalOp::execute() {
    auto child_gen = child_->execute();

    while (auto child_batch = co_await child_gen.next()) {
        RowBatch output;

        for (const auto& row : child_batch->rows) {
            // Find the source vertex ID in the row
            VertexId src_id = INVALID_VERTEX_ID;
            for (const auto& val : row) {
                if (std::holds_alternative<int64_t>(val)) {
                    src_id = static_cast<VertexId>(std::get<int64_t>(val));
                    break;
                } else if (std::holds_alternative<VertexValue>(val)) {
                    src_id = std::get<VertexValue>(val).id;
                    break;
                }
            }
            if (src_id == INVALID_VERTEX_ID) {
                continue;
            }

            // Scan edges from this vertex
            Direction dir = Direction::OUT;
            if (direction_ == cypher::RelationshipDirection::RIGHT_TO_LEFT) {
                dir = Direction::IN;
            } else if (direction_ == cypher::RelationshipDirection::UNDIRECTED) {
                dir = Direction::BOTH;
            }
            auto edge_gen = store_.scanEdges(src_id, dir, label_filter_);
            while (auto edge_batch = co_await edge_gen.next()) {
                for (const auto& entry : *edge_batch) {
                    Row new_row = row; // copy original columns

                    // Load destination vertex labels and properties
                    auto dst_labels = co_await store_.getVertexLabels(entry.neighbor_id);
                    VertexValue dst_vv;
                    dst_vv.id = entry.neighbor_id;
                    dst_vv.labels = dst_labels;
                    Properties merged_props;
                    for (LabelId lid : dst_labels) {
                        auto props = co_await store_.getVertexProperties(entry.neighbor_id, lid);
                        if (props) {
                            if (merged_props.size() < props->size()) {
                                merged_props.resize(props->size());
                            }
                            for (size_t i = 0; i < props->size(); i++) {
                                if ((*props)[i].has_value()) {
                                    merged_props[i] = std::move((*props)[i]);
                                }
                            }
                        }
                    }
                    dst_vv.properties = std::move(merged_props);
                    new_row.push_back(Value(std::move(dst_vv)));

                    // Add edge value if edge variable is bound
                    if (!edge_var_.empty()) {
                        EdgeValue ev;
                        ev.id = entry.edge_id;
                        ev.src_id = src_id;
                        ev.dst_id = entry.neighbor_id;
                        ev.label_id = entry.edge_label_id;
                        new_row.push_back(Value(std::move(ev)));
                    }
                    output.push_back(std::move(new_row));

                    if (output.size() >= RowBatch::CAPACITY) {
                        co_yield std::move(output);
                        output.clear();
                    }
                }
            }
        }

        if (!output.empty()) {
            co_yield std::move(output);
        }
    }
}

// ==================== Filter ====================

folly::coro::AsyncGenerator<RowBatch> FilterPhysicalOp::execute() {
    auto child_gen = child_->execute();

    while (auto batch = co_await child_gen.next()) {
        RowBatch output;
        for (const auto& row : batch->rows) {
            Value result = evaluator_.evaluate(predicate_, row, schema_);
            // Treat truthy values as passing the filter
            if (std::holds_alternative<bool>(result) && std::get<bool>(result)) {
                output.push_back(Row(row)); // copy
            }
        }
        if (!output.empty()) {
            co_yield std::move(output);
        }
    }
}

// ==================== Project ====================

folly::coro::AsyncGenerator<RowBatch> ProjectPhysicalOp::execute() {
    auto child_gen = child_->execute();

    while (auto batch = co_await child_gen.next()) {
        RowBatch output;
        for (const auto& row : batch->rows) {
            Row new_row;
            new_row.reserve(items_.size());
            for (const auto& item : items_) {
                Value val = evaluator_.evaluate(item.expr, row, input_schema_);
                new_row.push_back(std::move(val));
            }
            output.push_back(std::move(new_row));
        }
        if (!output.empty()) {
            co_yield std::move(output);
        }
    }
}

// ==================== Limit ====================

folly::coro::AsyncGenerator<RowBatch> LimitPhysicalOp::execute() {
    auto child_gen = child_->execute();
    int64_t remaining = limit_;

    while (auto batch = co_await child_gen.next()) {
        RowBatch output;
        for (auto& row : batch->rows) {
            if (remaining <= 0) {
                if (!output.empty()) {
                    co_yield std::move(output);
                }
                co_return;
            }
            output.push_back(std::move(row));
            --remaining;
        }
        if (!output.empty()) {
            co_yield std::move(output);
        }
        if (remaining <= 0) {
            co_return;
        }
    }
}

// ==================== Skip ====================

folly::coro::AsyncGenerator<RowBatch> SkipPhysicalOp::execute() {
    auto child_gen = child_->execute();
    int64_t remaining = skip_;

    while (auto batch = co_await child_gen.next()) {
        RowBatch output;
        for (auto& row : batch->rows) {
            if (remaining > 0) {
                --remaining;
                continue;
            }
            output.push_back(std::move(row));
        }
        if (!output.empty()) {
            co_yield std::move(output);
        }
    }
}

// ==================== Sort ====================

folly::coro::AsyncGenerator<RowBatch> SortPhysicalOp::execute() {
    // Materialize all rows from child
    std::vector<Row> all_rows;
    auto child_gen = child_->execute();
    while (auto batch = co_await child_gen.next()) {
        for (auto& row : batch->rows) {
            all_rows.push_back(std::move(row));
        }
    }

    // Sort using the sort items
    std::sort(all_rows.begin(), all_rows.end(), [this](const Row& a, const Row& b) {
        for (const auto& item : sort_items_) {
            Value va = evaluator_.evaluate(item.expr, a, input_schema_);
            Value vb = evaluator_.evaluate(item.expr, b, input_schema_);

            bool less = false, greater = false;
            std::visit(
                [&less, &greater](const auto& la, const auto& lb) {
                    using A = std::decay_t<decltype(la)>;
                    using B = std::decay_t<decltype(lb)>;
                    if constexpr ((std::is_same_v<A, int64_t> && std::is_same_v<B, int64_t>) ||
                                  (std::is_same_v<A, double> && std::is_same_v<B, double>)) {
                        less = la < lb;
                        greater = la > lb;
                    } else if constexpr (std::is_same_v<A, std::string> && std::is_same_v<B, std::string>) {
                        less = la < lb;
                        greater = la > lb;
                    }
                },
                va, vb);

            if (item.direction == cypher::OrderBy::Direction::DESC) {
                std::swap(less, greater);
            }
            if (less)
                return true;
            if (greater)
                return false;
        }
        return false;
    });

    // Yield sorted rows in batches
    RowBatch output;
    for (auto& row : all_rows) {
        output.push_back(std::move(row));
        if (output.size() >= RowBatch::CAPACITY) {
            co_yield std::move(output);
            output.clear();
        }
    }
    if (!output.empty()) {
        co_yield std::move(output);
    }
}

// ==================== Distinct ====================

folly::coro::AsyncGenerator<RowBatch> DistinctPhysicalOp::execute() {
    std::unordered_set<Row, RowHash, RowEqual> seen;

    auto child_gen = child_->execute();
    while (auto batch = co_await child_gen.next()) {
        RowBatch output;
        for (auto& row : batch->rows) {
            if (seen.insert(row).second) {
                output.push_back(std::move(row));
            }
        }
        if (!output.empty()) {
            co_yield std::move(output);
        }
    }
}

// ==================== Aggregate ====================

folly::coro::AsyncGenerator<RowBatch> AggregatePhysicalOp::execute() {
    // Per-aggregate accumulator state per group
    struct AggState {
        int64_t count = 0;
        double sum = 0;
        double min_val = std::numeric_limits<double>::max();
        double max_val = std::numeric_limits<double>::lowest();
        bool min_set = false;
        bool max_set = false;
        std::unordered_set<Value, ValueHash> distinct_values;
    };

    // Per-group state: vector of AggState, one per aggregate function
    struct GroupState {
        int64_t row_count = 0;
        std::vector<AggState> aggs;
    };

    // Map from group key row → accumulator state
    std::unordered_map<Row, GroupState, RowHash, RowEqual> groups;
    std::vector<Row> group_order;

    bool has_aggregates = !aggregates_.empty();

    auto child_gen = child_->execute();
    while (auto batch = co_await child_gen.next()) {
        for (const auto& row : batch->rows) {
            // Compute group key
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

            // Accumulate each aggregate
            for (size_t i = 0; i < aggregates_.size(); ++i) {
                const auto& agg = aggregates_[i];
                auto& as = state.aggs[i];

                // count(*) special case: no args → null literal sentinel
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
                        // Non-numeric: for count, still count; for others, skip
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

    // Output one row per group
    RowBatch output;
    for (const auto& key : group_order) {
        auto& state = groups.at(key);
        Row out_row;

        // Group key values
        for (const auto& v : key) {
            out_row.push_back(v);
        }

        // Aggregate result values
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

    // Global aggregate on empty data: output one row with zero/null results
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

// ==================== CreateNode ====================

folly::coro::AsyncGenerator<RowBatch> CreateNodePhysicalOp::execute() {
    // Execute child first if present (for chained creates)
    if (child_) {
        auto child_gen = child_->execute();
        while (auto batch = co_await child_gen.next()) {
            // Discard child output — we just need the side effects
        }
    }

    bool ok = co_await store_.insertVertex(assigned_vid_, label_props_,
                                           nullptr // no primary key in Phase 1
    );

    if (ok && label_defs_) {
        for (const auto& [label_id, props] : label_props_) {
            auto def_it = label_defs_->find(label_id);
            if (def_it == label_defs_->end())
                continue;
            for (const auto& idx : def_it->second.indexes) {
                if (idx.state != IndexState::WRITE_ONLY && idx.state != IndexState::PUBLIC)
                    continue;
                for (auto prop_id : idx.prop_ids) {
                    if (prop_id < props.size() && props[prop_id].has_value()) {
                        auto table = vidxTable(label_id, prop_id);
                        co_await store_.insertIndexEntry(table, props[prop_id].value(), assigned_vid_);
                    }
                }
            }
        }
    }

    RowBatch output;
    if (ok) {
        Row row;
        row.push_back(Value(static_cast<int64_t>(assigned_vid_)));
        output.push_back(std::move(row));
    }
    if (!output.empty()) {
        co_yield std::move(output);
    }
}

// ==================== CreateEdge ====================

folly::coro::AsyncGenerator<RowBatch> CreateEdgePhysicalOp::execute() {
    // Execute child first (creates source/destination vertices)
    if (child_) {
        auto child_gen = child_->execute();
        while (auto batch = co_await child_gen.next()) {
            // Discard child output
        }
    }

    bool ok = co_await store_.insertEdge(assigned_eid_, src_id_, dst_id_, label_id_,
                                         0, // seq = 0 for first edge between this pair
                                         {} // no properties in Phase 1
    );

    RowBatch output;
    if (ok) {
        Row row;
        row.push_back(Value(static_cast<int64_t>(assigned_eid_)));
        output.push_back(std::move(row));
    }
    if (!output.empty()) {
        co_yield std::move(output);
    }
}

} // namespace compute
} // namespace eugraph
