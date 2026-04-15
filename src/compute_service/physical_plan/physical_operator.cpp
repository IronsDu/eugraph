#include "compute_service/physical_plan/physical_operator.hpp"

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
            // Load properties for this vertex
            auto props = co_await store_.getVertexProperties(vid, label_id_);
            VertexValue vv;
            vv.id = vid;
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
