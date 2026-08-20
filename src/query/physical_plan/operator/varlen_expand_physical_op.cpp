#include "query/physical_plan/operator/varlen_expand_physical_op.hpp"
#include "common/types/constants.hpp"
#include "query/dataset/row.hpp"
#include "storage/data/i_sync_graph_data_store.hpp"

#include <spdlog/spdlog.h>

namespace eugraph {
namespace compute {

namespace {
VertexId vertexIdFromValue(const Value& val) {
    if (std::holds_alternative<VertexValue>(val))
        return std::get<VertexValue>(val).id;
    if (std::holds_alternative<VertexRef>(val))
        return std::get<VertexRef>(val).id;
    if (std::holds_alternative<int64_t>(val))
        return static_cast<VertexId>(std::get<int64_t>(val));
    return INVALID_VERTEX_ID;
}
} // anonymous namespace

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

    auto loadVertex = [&](VertexId vid) -> folly::coro::Task<VertexValue> {
        VertexValue vv;
        vv.id = vid;
        auto labels = co_await store_.getVertexLabels(vid);
        vv.labels = labels;
        for (auto lid : labels) {
            auto props = co_await store_.getVertexProperties(vid, lid);
            if (props)
                vv.properties[lid] = std::move(*props);
        }
        co_return vv;
    };

    auto loadEdge = [&](EdgeId eid, EdgeLabelId elid, VertexId src, VertexId dst,
                        uint64_t seq) -> folly::coro::Task<EdgeValue> {
        EdgeValue ev;
        ev.id = eid;
        ev.src_id = src;
        ev.dst_id = dst;
        ev.label_id = elid;
        ev.seq = seq;
        auto props = co_await store_.getEdgeProperties(elid, eid);
        if (props)
            ev.properties = std::move(*props);
        co_return ev;
    };

    // For UNDIRECTED, BOTH merges OUT/IN hits without per-edge direction.
    // Split into two scans so each edge's physical src/dst can be recovered.
    bool split_undirected = (dir == Direction::BOTH);

    // Wrapper carrying direction alongside each edge so path/edge
    // serialization can render the correct arrow direction.
    struct DirectedEdgeEntry {
        VertexId neighbor_id;
        EdgeId edge_id;
        EdgeLabelId edge_label_id;
        uint64_t seq;
        bool physical_out = true; // true: found via OUT adjacency
    };

    auto scanDirected = [&](VertexId vid, Direction scan_dir) -> folly::coro::Task<std::vector<DirectedEdgeEntry>> {
        std::vector<DirectedEdgeEntry> out;
        bool phy_out = (scan_dir == Direction::OUT);
        for (const auto& label_filter : scan_filters) {
            auto edge_gen = store_.scanEdges(vid, scan_dir, label_filter);
            while (auto edge_batch = co_await edge_gen.next()) {
                for (const auto& e : *edge_batch)
                    out.push_back({e.neighbor_id, e.edge_id, e.edge_label_id, e.seq, phy_out});
            }
        }
        co_return out;
    };

    auto hasDstLabels = [&](VertexId vid) -> folly::coro::Task<bool> {
        if (dst_label_ids_.empty())
            co_return true;
        auto labels = co_await store_.getVertexLabels(vid);
        for (LabelId need : dst_label_ids_) {
            if (labels.find(need) == labels.end())
                co_return false;
        }
        co_return true;
    };

    auto scanAll = [&](VertexId vid) -> folly::coro::Task<std::vector<DirectedEdgeEntry>> {
        if (split_undirected) {
            auto out = co_await scanDirected(vid, Direction::OUT);
            auto in = co_await scanDirected(vid, Direction::IN);
            out.insert(out.end(), std::make_move_iterator(in.begin()), std::make_move_iterator(in.end()));
            co_return out;
        }
        co_return co_await scanDirected(vid, dir);
    };

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
            std::vector<DirectedEdgeEntry> edges;
            size_t edge_idx;
            EdgeVisitKey incoming_key;
            bool has_incoming;
            EdgeId incoming_edge_id = INVALID_EDGE_ID;
            EdgeLabelId incoming_edge_label_id = INVALID_EDGE_LABEL_ID;
            uint64_t incoming_edge_seq = 0;
            // Physical src/dst of the incoming edge so loadEdge gets the
            // correct direction regardless of traversal direction.
            VertexId incoming_physical_src = INVALID_VERTEX_ID;
            VertexId incoming_physical_dst = INVALID_VERTEX_ID;
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
            std::vector<DirectedEdgeEntry> start_edges = co_await scanAll(src_id);

            // Emit identity path when min_hops == 0 (zero-hop: src == dst)
            if (min_hops_ == 0 && co_await hasDstLabels(src_id)) {
                if (dst_bound_) {
                    VertexId bound_dst = vertexIdFromValue(rows[src_row][dst_col_idx_]);
                    if (bound_dst != src_id)
                        continue;
                }
                OutputEntry identity_entry;
                identity_entry.src_row = src_row;
                identity_entry.dst_id = src_id;
                if (!path_var_.empty()) {
                    PathValue pv;
                    const auto& src_val = rows[src_row][src_col_idx_];
                    if (std::holds_alternative<VertexValue>(src_val)) {
                        pv.elements.push_back(ValueStorage{src_val});
                    } else {
                        pv.elements.push_back(ValueStorage{Value(co_await loadVertex(src_id))});
                    }
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

                EdgeVisitKey edge_key{edge.edge_id};
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

                if (next_depth >= min_hops_ && co_await hasDstLabels(edge.neighbor_id)) {
                    OutputEntry entry;
                    entry.src_row = src_row;
                    entry.dst_id = edge.neighbor_id;
                    if (!path_var_.empty()) {
                        // Build path: stack vertices/edges + current edge + destination
                        PathValue pv;
                        const auto& src_val = rows[src_row][src_col_idx_];
                        if (std::holds_alternative<VertexValue>(src_val)) {
                            pv.elements.push_back(ValueStorage{src_val});
                        } else {
                            pv.elements.push_back(ValueStorage{Value(co_await loadVertex(src_id))});
                        }
                        for (size_t si = 1; si < stack.size(); ++si) {
                            pv.elements.push_back(ValueStorage{Value(
                                co_await loadEdge(stack[si].incoming_edge_id, stack[si].incoming_edge_label_id,
                                                  stack[si].incoming_physical_src, stack[si].incoming_physical_dst,
                                                  stack[si].incoming_edge_seq))});
                            pv.elements.push_back(ValueStorage{Value(co_await loadVertex(stack[si].vertex))});
                        }
                        // Add current edge and destination vertex
                        VertexId cur_phys_src = edge.physical_out ? frame.vertex : edge.neighbor_id;
                        VertexId cur_phys_dst = edge.physical_out ? edge.neighbor_id : frame.vertex;
                        pv.elements.push_back(ValueStorage{Value(co_await loadEdge(
                            edge.edge_id, edge.edge_label_id, cur_phys_src, cur_phys_dst, edge.seq))});
                        VertexValue dst_vv = co_await loadVertex(edge.neighbor_id);
                        pv.elements.push_back(ValueStorage{Value(std::move(dst_vv))});
                        entry.path = std::move(pv);
                    }
                    // P2: build edge list for named edge variable
                    if (!edge_var_.empty()) {
                        ListValue lv;
                        for (size_t si = 1; si < stack.size(); ++si) {
                            lv.elements.push_back(ValueStorage{Value(
                                co_await loadEdge(stack[si].incoming_edge_id, stack[si].incoming_edge_label_id,
                                                  stack[si].incoming_physical_src, stack[si].incoming_physical_dst,
                                                  stack[si].incoming_edge_seq))});
                        }
                        VertexId el_phys_src = edge.physical_out ? frame.vertex : edge.neighbor_id;
                        VertexId el_phys_dst = edge.physical_out ? edge.neighbor_id : frame.vertex;
                        lv.elements.push_back(ValueStorage{Value(
                            co_await loadEdge(edge.edge_id, edge.edge_label_id, el_phys_src, el_phys_dst, edge.seq))});
                        entry.edge_list = std::move(lv);
                    }
                    if (dst_bound_) {
                        VertexId bound_dst = vertexIdFromValue(rows[src_row][dst_col_idx_]);
                        if (bound_dst != edge.neighbor_id)
                            continue;
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

                        if (!dst_bound_) {
                            for (size_t i = 0; i < output_buffer.size(); ++i) {
                                size_t dst_col_idx = input_cols;
                                output.setValue(dst_col_idx, i, Value(VertexRef{output_buffer[i].dst_id}));
                            }
                        }
                        // Fill path column if present
                        if (!path_var_.empty()) {
                            size_t path_col_idx = input_cols + (dst_bound_ ? 0 : 1);
                            for (size_t i = 0; i < output_buffer.size(); ++i) {
                                output.setValue(path_col_idx, i, Value(output_buffer[i].path));
                            }
                        }
                        // Fill edge list column if present
                        if (!edge_var_.empty()) {
                            size_t edge_col_idx = input_cols + (dst_bound_ ? 0 : 1) + (path_var_.empty() ? 0 : 1);
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
                    // P2: vertex cycle detection is only required for truly
                    // unbounded traversals. Bounded varlen paths may legally
                    // revisit a vertex as long as every relationship is used
                    // at most once.
                    if (max_hops_ < 0) {
                        bool vertex_on_path = false;
                        for (const auto& f : stack) {
                            if (f.vertex == edge.neighbor_id) {
                                vertex_on_path = true;
                                break;
                            }
                        }
                        if (vertex_on_path)
                            continue;
                    }

                    // Go deeper: collect edges from neighbor
                    visited_edges.insert(edge_key);

                    auto next_edges = co_await scanAll(edge.neighbor_id);

                    VertexId push_physical_src = edge.physical_out ? frame.vertex : edge.neighbor_id;
                    VertexId push_physical_dst = edge.physical_out ? edge.neighbor_id : frame.vertex;
                    stack.push_back({edge.neighbor_id, next_depth, std::move(next_edges), 0, edge_key, true,
                                     edge.edge_id, edge.edge_label_id, edge.seq, push_physical_src, push_physical_dst});
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

            if (!dst_bound_) {
                for (size_t i = 0; i < output_buffer.size(); ++i) {
                    size_t dst_col_idx = input_cols;
                    output.setValue(dst_col_idx, i, Value(VertexRef{output_buffer[i].dst_id}));
                }
            }
            // Fill path column if present
            if (!path_var_.empty()) {
                size_t path_col_idx = input_cols + (dst_bound_ ? 0 : 1);
                for (size_t i = 0; i < output_buffer.size(); ++i) {
                    output.setValue(path_col_idx, i, Value(output_buffer[i].path));
                }
            }
            // Fill edge list column if present
            if (!edge_var_.empty()) {
                size_t edge_col_idx = input_cols + (dst_bound_ ? 0 : 1) + (path_var_.empty() ? 0 : 1);
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
