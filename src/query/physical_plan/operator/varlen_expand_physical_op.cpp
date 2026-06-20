#include "query/physical_plan/operator/varlen_expand_physical_op.hpp"
#include "common/types/constants.hpp"
#include "query/dataset/row.hpp"
#include "storage/data/i_sync_graph_data_store.hpp"

namespace eugraph {
namespace compute {

VarLenExpandPhysicalOp::~VarLenExpandPhysicalOp() = default;

std::string VarLenExpandPhysicalOp::toString() const {
    std::string dir;
    switch (direction_) {
    case cypher::RelationshipDirection::LEFT_TO_RIGHT:
        dir = "OUT";
        break;
    case cypher::RelationshipDirection::RIGHT_TO_LEFT:
        dir = "IN";
        break;
    case cypher::RelationshipDirection::UNDIRECTED:
        dir = "ANY";
        break;
    }
    auto s = "VarLenExpand(src=" + src_var_ + ", dst=" + dst_var_ + ", hops=[" + std::to_string(min_hops_) + ".." +
             std::to_string(max_hops_) + "]";
    if (label_filters_ && !label_filters_->empty()) {
        s += ", labels=[";
        for (size_t i = 0; i < label_filters_->size(); ++i) {
            if (i > 0)
                s += ",";
            s += std::to_string((*label_filters_)[i]);
        }
        s += "]";
    }
    s += ", direction=" + dir + ")";
    return s;
}

folly::coro::AsyncGenerator<DataChunk> VarLenExpandPhysicalOp::executeChunk() {
    auto child_gen = child_->executeChunk();

    auto dir = Direction::OUT;
    if (direction_ == cypher::RelationshipDirection::RIGHT_TO_LEFT) {
        dir = Direction::IN;
    } else if (direction_ == cypher::RelationshipDirection::UNDIRECTED) {
        dir = Direction::BOTH;
    }

    std::vector<std::optional<EdgeLabelId>> scan_filters;
    if (!label_filters_.has_value()) {
        scan_filters.push_back(std::nullopt);
    } else {
        for (auto lid : *label_filters_) {
            scan_filters.push_back(lid);
        }
    }

    while (auto chunk = co_await child_gen.next()) {
        auto rows = chunk->toRows();
        size_t input_cols = chunk->numColumns();

        struct OutputEntry {
            size_t src_row;
            VertexId dst_id;
            PathValue path;
            ListValue edge_list;
        };
        std::vector<OutputEntry> output_buffer;
        output_buffer.reserve(DataChunk::DEFAULT_CAPACITY);

        // DFS stack frame
        struct StackFrame {
            VertexId vertex;
            int depth;
            std::vector<ISyncGraphDataStore::EdgeIndexEntry> edges;
            size_t edge_idx;
            EdgeVisitKey incoming_key;
            bool has_incoming;
            EdgeId incoming_edge_id = INVALID_EDGE_ID;
            EdgeLabelId incoming_edge_label_id = INVALID_EDGE_LABEL_ID;
            uint64_t incoming_edge_seq = 0;
        };

        for (size_t src_row = 0; src_row < rows.size(); ++src_row) {
            VertexId src_id = INVALID_VERTEX_ID;
            if (src_col_idx_ >= 0 && static_cast<size_t>(src_col_idx_) < rows[src_row].size()) {
                const auto& val = rows[src_row][src_col_idx_];
                if (std::holds_alternative<VertexValue>(val)) {
                    src_id = std::get<VertexValue>(val).id;
                } else if (std::holds_alternative<VertexRef>(val)) {
                    src_id = std::get<VertexRef>(val).id;
                } else if (std::holds_alternative<int64_t>(val)) {
                    src_id = static_cast<VertexId>(std::get<int64_t>(val));
                }
            }
            if (src_id == INVALID_VERTEX_ID)
                continue;

            // Collect initial edges from source vertex
            std::vector<ISyncGraphDataStore::EdgeIndexEntry> start_edges;
            for (const auto& label_filter : scan_filters) {
                auto edge_gen = store_.scanEdges(src_id, dir, label_filter);
                while (auto edge_batch = co_await edge_gen.next()) {
                    start_edges.insert(start_edges.end(), edge_batch->begin(), edge_batch->end());
                }
            }

            // Emit identity path when min_hops == 0 (zero-hop: src == dst)
            if (min_hops_ == 0) {
                OutputEntry identity_entry;
                identity_entry.src_row = src_row;
                identity_entry.dst_id = src_id;
                if (!path_var_.empty()) {
                    PathValue pv;
                    pv.elements.push_back(ValueStorage{rows[src_row][src_col_idx_]});
                    identity_entry.path = std::move(pv);
                }
                if (!edge_var_.empty()) {
                    identity_entry.edge_list = ListValue{};
                }
                output_buffer.push_back(std::move(identity_entry));
            }

            // If only zero-hop requested, skip edge scanning and DFS
            if (max_hops_ == 0)
                continue;

            if (start_edges.empty())
                continue;

            std::unordered_set<EdgeVisitKey, EdgeVisitKeyHash> visited_edges;

            std::vector<StackFrame> stack;
            stack.reserve(static_cast<size_t>(max_hops_) + 1);
            stack.push_back({src_id, 0, std::move(start_edges), 0, {}, false});

            while (!stack.empty()) {
                auto& frame = stack.back();

                if (frame.edge_idx >= frame.edges.size()) {
                    // Backtrack: remove incoming edge from visited set
                    if (frame.has_incoming) {
                        visited_edges.erase(frame.incoming_key);
                    }
                    stack.pop_back();
                    continue;
                }

                const auto& edge = frame.edges[frame.edge_idx];
                frame.edge_idx++;

                EdgeVisitKey edge_key{frame.vertex, edge.edge_label_id, edge.neighbor_id, edge.seq};
                if (visited_edges.count(edge_key))
                    continue;

                // P3: check edge property filters for this edge's label
                if (!edge_prop_filters_.empty()) {
                    auto fit = edge_prop_filters_.find(edge.edge_label_id);
                    if (fit != edge_prop_filters_.end() && !fit->second.empty()) {
                        std::vector<uint16_t> prop_ids;
                        for (const auto& [pid, _] : fit->second)
                            prop_ids.push_back(pid);
                        auto props = co_await store_.getEdgeProperties(edge.edge_label_id, edge.edge_id, prop_ids);
                        bool pass = true;
                        if (props) {
                            for (const auto& [pid, expected] : fit->second) {
                                if (pid >= props->size() || !props->at(pid).has_value() ||
                                    props->at(pid).value() != expected) {
                                    pass = false;
                                    break;
                                }
                            }
                        } else {
                            pass = false;
                        }
                        if (!pass)
                            continue;
                    }
                }

                int next_depth = frame.depth + 1;

                if (next_depth >= min_hops_) {
                    OutputEntry entry;
                    entry.src_row = src_row;
                    entry.dst_id = edge.neighbor_id;
                    if (!path_var_.empty()) {
                        // Build path: stack vertices/edges + current edge + destination
                        PathValue pv;
                        pv.elements.push_back(ValueStorage{rows[src_row][src_col_idx_]});
                        for (size_t si = 1; si < stack.size(); ++si) {
                            EdgeValue ev;
                            ev.id = stack[si].incoming_edge_id;
                            ev.src_id = stack[si - 1].vertex;
                            ev.dst_id = stack[si].vertex;
                            ev.label_id = stack[si].incoming_edge_label_id;
                            ev.seq = stack[si].incoming_edge_seq;
                            pv.elements.push_back(ValueStorage{Value(std::move(ev))});
                            VertexValue vv;
                            vv.id = stack[si].vertex;
                            vv.labels = co_await store_.getVertexLabels(stack[si].vertex);
                            pv.elements.push_back(ValueStorage{Value(std::move(vv))});
                        }
                        // Add current edge and destination vertex
                        EdgeValue ev;
                        ev.id = edge.edge_id;
                        ev.src_id = frame.vertex;
                        ev.dst_id = edge.neighbor_id;
                        ev.label_id = edge.edge_label_id;
                        ev.seq = edge.seq;
                        pv.elements.push_back(ValueStorage{Value(std::move(ev))});
                        VertexValue dst_vv;
                        dst_vv.id = edge.neighbor_id;
                        dst_vv.labels = co_await store_.getVertexLabels(edge.neighbor_id);
                        pv.elements.push_back(ValueStorage{Value(std::move(dst_vv))});
                        entry.path = std::move(pv);
                    }
                    // P2: build edge list for named edge variable
                    if (!edge_var_.empty()) {
                        ListValue lv;
                        for (size_t si = 1; si < stack.size(); ++si) {
                            EdgeValue ev;
                            ev.id = stack[si].incoming_edge_id;
                            ev.src_id = stack[si - 1].vertex;
                            ev.dst_id = stack[si].vertex;
                            ev.label_id = stack[si].incoming_edge_label_id;
                            ev.seq = stack[si].incoming_edge_seq;
                            lv.elements.push_back(ValueStorage{Value(std::move(ev))});
                        }
                        EdgeValue ev;
                        ev.id = edge.edge_id;
                        ev.src_id = frame.vertex;
                        ev.dst_id = edge.neighbor_id;
                        ev.label_id = edge.edge_label_id;
                        ev.seq = edge.seq;
                        lv.elements.push_back(ValueStorage{Value(std::move(ev))});
                        entry.edge_list = std::move(lv);
                    }
                    output_buffer.push_back(std::move(entry));

                    if (output_buffer.size() >= DataChunk::DEFAULT_CAPACITY) {
                        // Build and yield a full DataChunk
                        DataChunk output;
                        output.columns.reserve(output_types_.size());

                        for (size_t c = 0; c < input_cols; ++c) {
                            auto& src_col = chunk->columns[c];
                            if ((src_col.form == VectorForm::FLAT || src_col.form == VectorForm::DICTIONARY) &&
                                src_col.buffer) {
                                SelectionVector mapped;
                                mapped.is_identity = false;
                                mapped.indices.reserve(output_buffer.size());
                                for (const auto& e : output_buffer) {
                                    uint32_t physical = static_cast<uint32_t>(e.src_row);
                                    if (src_col.form == VectorForm::DICTIONARY) {
                                        mapped.indices.push_back(src_col.dict_sel[physical]);
                                    } else {
                                        mapped.indices.push_back(physical);
                                    }
                                }
                                mapped.count = output_buffer.size();
                                output.columns.push_back(Column::dict(src_col.buffer, mapped));
                            } else if (src_col.form == VectorForm::CONSTANT) {
                                output.columns.push_back(Column::constant(src_col.constant_value));
                            } else {
                                output.columns.push_back(Column::flat(src_col.type, output_buffer.size()));
                            }
                        }

                        for (size_t c = input_cols; c < output_types_.size(); ++c) {
                            output.columns.push_back(Column::flat(output_types_[c].kind, output_buffer.size()));
                        }

                        for (size_t i = 0; i < output_buffer.size(); ++i) {
                            size_t dst_col_idx = input_cols;
                            output.setValue(dst_col_idx, i, Value(VertexRef{output_buffer[i].dst_id}));
                        }
                        // Fill path column if present
                        if (!path_var_.empty()) {
                            size_t path_col_idx = input_cols + 1;
                            for (size_t i = 0; i < output_buffer.size(); ++i) {
                                output.setValue(path_col_idx, i, Value(output_buffer[i].path));
                            }
                        }
                        // Fill edge list column if present
                        if (!edge_var_.empty()) {
                            size_t edge_col_idx = input_cols + 1 + (path_var_.empty() ? 0 : 1);
                            for (size_t i = 0; i < output_buffer.size(); ++i) {
                                output.setValue(edge_col_idx, i, Value(output_buffer[i].edge_list));
                            }
                        }
                        output.count = output_buffer.size();
                        co_yield std::move(output);
                        output_buffer.clear();
                    }
                }

                // P2: unbounded depth check (max_hops_ < 0 means unbounded)
                if (max_hops_ < 0 || next_depth < max_hops_) {
                    // P2: vertex cycle detection — prevent infinite loops on unbounded
                    bool vertex_on_path = false;
                    for (const auto& f : stack) {
                        if (f.vertex == edge.neighbor_id) {
                            vertex_on_path = true;
                            break;
                        }
                    }
                    if (vertex_on_path)
                        continue;

                    // Go deeper: collect edges from neighbor
                    visited_edges.insert(edge_key);

                    std::vector<ISyncGraphDataStore::EdgeIndexEntry> next_edges;
                    for (const auto& label_filter : scan_filters) {
                        auto edge_gen = store_.scanEdges(edge.neighbor_id, dir, label_filter);
                        while (auto edge_batch = co_await edge_gen.next()) {
                            next_edges.insert(next_edges.end(), edge_batch->begin(), edge_batch->end());
                        }
                    }

                    stack.push_back({edge.neighbor_id, next_depth, std::move(next_edges), 0, edge_key, true,
                                     edge.edge_id, edge.edge_label_id, edge.seq});
                }
            }
        }

        // Yield remaining output
        if (!output_buffer.empty()) {
            DataChunk output;
            output.columns.reserve(output_types_.size());

            for (size_t c = 0; c < input_cols; ++c) {
                auto& src_col = chunk->columns[c];
                if ((src_col.form == VectorForm::FLAT || src_col.form == VectorForm::DICTIONARY) && src_col.buffer) {
                    SelectionVector mapped;
                    mapped.is_identity = false;
                    mapped.indices.reserve(output_buffer.size());
                    for (const auto& e : output_buffer) {
                        uint32_t physical = static_cast<uint32_t>(e.src_row);
                        if (src_col.form == VectorForm::DICTIONARY) {
                            mapped.indices.push_back(src_col.dict_sel[physical]);
                        } else {
                            mapped.indices.push_back(physical);
                        }
                    }
                    mapped.count = output_buffer.size();
                    output.columns.push_back(Column::dict(src_col.buffer, mapped));
                } else if (src_col.form == VectorForm::CONSTANT) {
                    output.columns.push_back(Column::constant(src_col.constant_value));
                } else {
                    output.columns.push_back(Column::flat(src_col.type, output_buffer.size()));
                }
            }

            for (size_t c = input_cols; c < output_types_.size(); ++c) {
                output.columns.push_back(Column::flat(output_types_[c].kind, output_buffer.size()));
            }

            for (size_t i = 0; i < output_buffer.size(); ++i) {
                size_t dst_col_idx = input_cols;
                output.setValue(dst_col_idx, i, Value(VertexRef{output_buffer[i].dst_id}));
            }
            // Fill path column if present
            if (!path_var_.empty()) {
                size_t path_col_idx = input_cols + 1;
                for (size_t i = 0; i < output_buffer.size(); ++i) {
                    output.setValue(path_col_idx, i, Value(output_buffer[i].path));
                }
            }
            // Fill edge list column if present
            if (!edge_var_.empty()) {
                size_t edge_col_idx = input_cols + 1 + (path_var_.empty() ? 0 : 1);
                for (size_t i = 0; i < output_buffer.size(); ++i) {
                    output.setValue(edge_col_idx, i, Value(output_buffer[i].edge_list));
                }
            }
            output.count = output_buffer.size();
            co_yield std::move(output);
        }
    }
}

} // namespace compute
} // namespace eugraph
