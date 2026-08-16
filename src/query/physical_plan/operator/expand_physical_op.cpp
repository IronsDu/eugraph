#include "query/physical_plan/operator/expand_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "query/dataset/row.hpp"

namespace eugraph {
namespace compute {

std::string ExpandPhysicalOp::toString() const {
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
    auto s = "Expand(src=" + src_var_ + ", dst=" + dst_var_;
    if (!edge_var_.empty())
        s += ", edge=" + edge_var_;
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

folly::coro::AsyncGenerator<DataChunk> ExpandPhysicalOp::executeChunk() {
    auto child_gen = child_->executeChunk();

    auto dir = Direction::OUT;
    if (direction_ == cypher::RelationshipDirection::RIGHT_TO_LEFT) {
        dir = Direction::IN;
    } else if (direction_ == cypher::RelationshipDirection::UNDIRECTED) {
        dir = Direction::BOTH;
    }
    // For UNDIRECTED, we split BOTH into OUT then IN scans so each EdgeKey
    // carries the physical src/dst of the stored edge. Scanning both lists
    // in one call (Direction::BOTH) merges entries without per-edge
    // direction info, forcing Expand to guess and producing wrong path
    // rendering (<-[:T]- vs -[:T]->) for undirected traversals.
    bool split_undirected = (dir == Direction::BOTH);

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

        // Collect output rows: DICTIONARY input + FLAT new columns
        // First pass: collect edges per source row
        struct EdgeEntry {
            size_t src_row;
            VertexId dst_id;
            EdgeId edge_id;
            EdgeLabelId edge_label_id;
            uint64_t seq;
            // For UNDIRECTED split: true when the edge was found via the OUT
            // adjacency list (physical src = liker, dst = neighbor); false
            // when found via IN (physical src = neighbor, dst = liker).
            bool physical_out = true;
        };
        std::vector<EdgeEntry> edges;

        auto scanOneDirection = [&](VertexId src_id, size_t src_row, Direction scan_dir) -> folly::coro::Task<void> {
            for (const auto& label_filter : scan_filters) {
                auto edge_gen = store_.scanEdges(src_id, scan_dir, label_filter);
                while (auto edge_batch = co_await edge_gen.next()) {
                    for (const auto& entry : *edge_batch) {
                        // Filter by destination vertex label constraint (e.g. (b:Person))
                        if (!dst_label_ids_.empty()) {
                            auto labels = co_await store_.getVertexLabels(entry.neighbor_id);
                            bool ok = true;
                            for (LabelId need : dst_label_ids_) {
                                if (labels.find(need) == labels.end()) {
                                    ok = false;
                                    break;
                                }
                            }
                            if (!ok)
                                continue;
                        }
                        bool phy_out = (scan_dir == Direction::OUT);
                        edges.push_back(
                            {src_row, entry.neighbor_id, entry.edge_id, entry.edge_label_id, entry.seq, phy_out});
                    }
                }
            }
            co_return;
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

            if (split_undirected) {
                size_t before_out = edges.size();
                co_await scanOneDirection(src_id, src_row, Direction::OUT);
                size_t before_in = edges.size();
                co_await scanOneDirection(src_id, src_row, Direction::IN);
                // For self-loops the same edge appears in both OUT and IN
                // adjacency. Track edge ids from this row's OUT scan and
                // remove IN edges that duplicate them.
                std::unordered_set<EdgeId> seen;
                for (size_t ei = before_out; ei < before_in; ++ei)
                    seen.insert(edges[ei].edge_id);
                edges.erase(std::remove_if(edges.begin() + static_cast<long>(before_in), edges.end(),
                                           [&seen](const EdgeEntry& e) {
                                               if (seen.count(e.edge_id))
                                                   return true;
                                               seen.insert(e.edge_id);
                                               return false;
                                           }),
                            edges.end());
            } else {
                for (const auto& label_filter : scan_filters) {
                    auto edge_gen = store_.scanEdges(src_id, dir, label_filter);
                    while (auto edge_batch = co_await edge_gen.next()) {
                        for (const auto& entry : *edge_batch) {
                            if (!dst_label_ids_.empty()) {
                                auto labels = co_await store_.getVertexLabels(entry.neighbor_id);
                                bool ok = true;
                                for (LabelId need : dst_label_ids_) {
                                    if (labels.find(need) == labels.end()) {
                                        ok = false;
                                        break;
                                    }
                                }
                                if (!ok)
                                    continue;
                            }
                            bool phy_out = (dir == Direction::OUT);
                            edges.push_back(
                                {src_row, entry.neighbor_id, entry.edge_id, entry.edge_label_id, entry.seq, phy_out});
                        }
                    }
                }
            }
        }

        if (edges.empty())
            continue;

        // Build output DataChunk
        DataChunk output;
        output.columns.reserve(output_types_.size());

        // Input columns: DICTIONARY sharing child buffer
        for (size_t c = 0; c < input_cols; ++c) {
            auto& src_col = chunk->columns[c];
            if ((src_col.form == VectorForm::FLAT || src_col.form == VectorForm::DICTIONARY) && src_col.buffer) {
                SelectionVector mapped;
                mapped.is_identity = false;
                mapped.indices.reserve(edges.size());
                for (const auto& e : edges) {
                    uint32_t physical = static_cast<uint32_t>(e.src_row);
                    if (src_col.form == VectorForm::DICTIONARY) {
                        mapped.indices.push_back(src_col.dict_sel[physical]);
                    } else {
                        mapped.indices.push_back(physical);
                    }
                }
                mapped.count = edges.size();
                output.columns.push_back(Column::dict(src_col.buffer, mapped));
            } else if (src_col.form == VectorForm::CONSTANT) {
                output.columns.push_back(Column::constant(src_col.constant_value));
            } else {
                output.columns.push_back(Column::flat(src_col.type, edges.size()));
            }
        }

        // New columns: FLAT (dst vertex, edge)
        for (size_t c = input_cols; c < output_types_.size(); ++c) {
            output.columns.push_back(Column::flat(output_types_[c].kind, edges.size()));
        }

        // Fill new columns: edge first, then dst (matches binder column index order)
        for (size_t i = 0; i < edges.size(); ++i) {
            size_t edge_col_idx = input_cols;
            size_t dst_col_idx = input_cols + (edge_var_.empty() ? 0 : 1);

            if (!dst_var_.empty()) {
                VertexRef dst_ref{edges[i].dst_id};
                output.setValue(dst_col_idx, i, Value(dst_ref));
            }
            if (!edge_var_.empty()) {
                VertexId sid = INVALID_VERTEX_ID;
                if (src_col_idx_ >= 0 && static_cast<size_t>(src_col_idx_) < rows[edges[i].src_row].size()) {
                    const auto& val = rows[edges[i].src_row][src_col_idx_];
                    if (std::holds_alternative<VertexValue>(val))
                        sid = std::get<VertexValue>(val).id;
                    else if (std::holds_alternative<VertexRef>(val))
                        sid = std::get<VertexRef>(val).id;
                }
                // EdgeKey.src_id / dst_id must reflect the physically stored
                // edge direction so path rendering (`<-[:T]-` vs `-[:T]->`)
                // is correct for undirected traversals. For Direction::IN
                // hits during an UNDIRECTED scan, the physical src is the
                // neighbor and the physical dst is the current vertex.
                VertexId physical_src = edges[i].physical_out ? sid : edges[i].dst_id;
                VertexId physical_dst = edges[i].physical_out ? edges[i].dst_id : sid;
                EdgeKey ek{edges[i].edge_id, physical_src, physical_dst, edges[i].edge_label_id,
                           static_cast<uint32_t>(edges[i].seq)};
                output.setValue(edge_col_idx, i, Value(std::move(ek)));
            }
        }
        output.count = edges.size();
        co_yield std::move(output);
    }
}

} // namespace compute
} // namespace eugraph
