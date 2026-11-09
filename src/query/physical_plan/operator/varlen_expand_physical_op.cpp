#include "query/physical_plan/operator/varlen_expand_physical_op.hpp"

#include "common/types/constants.hpp"
#include "query/dataset/row.hpp"
#include "query/evaluator/expression_evaluator.hpp"
#include "query/physical_plan/expression_compiler.hpp"
#include "query/physical_plan/operator/property_value_convert.hpp"
#include "storage/data/i_sync_graph_data_store.hpp"

#include <spdlog/spdlog.h>

namespace eugraph {
namespace compute {

namespace {
VertexId vertexIdFromValue(const Value& val) {
    if (std::holds_alternative<VertexValuePtr>(val))
        return (*std::get<VertexValuePtr>(val)).id;
    if (std::holds_alternative<VertexRef>(val))
        return std::get<VertexRef>(val).id;
    if (std::holds_alternative<int64_t>(val))
        return static_cast<VertexId>(std::get<int64_t>(val));
    return INVALID_VERTEX_ID;
}

EdgeId edgeIdFromValue(const Value& val) {
    if (const auto* e = std::get_if<EdgeValuePtr>(&val))
        return (*e)->id;
    if (const auto* k = std::get_if<EdgeKey>(&val))
        return k->id;
    return INVALID_EDGE_ID;
}

std::vector<EdgeId> edgeIdsFromValue(const Value& val) {
    std::vector<EdgeId> ids;
    if (const auto* lv = std::get_if<ListValuePtr>(&val)) {
        for (const auto& item : (*lv)->elements) {
            const Value& ev = item.value;
            if (const auto* e = std::get_if<EdgeValuePtr>(&ev))
                ids.push_back((*e)->id);
            else if (const auto* k = std::get_if<EdgeKey>(&ev))
                ids.push_back(k->id);
        }
    }
    return ids;
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

void VarLenExpandPhysicalOp::compileExpressions(const TupleSlotLayout& input_layout) {
    if (edge_prop_filter_exprs_.empty())
        return;
    ExpressionCompiler compiler(input_layout);
    for (auto& [label_id, filters] : edge_prop_filter_exprs_) {
        (void)label_id;
        for (auto& [prop_id, expr] : filters) {
            (void)prop_id;
            if (expr)
                compiler.compile(*expr);
        }
    }
}

folly::coro::AsyncGenerator<DataChunk> VarLenExpandPhysicalOp::executeChunk() {
    if (dst_label_missing_)
        co_return;
    auto child_gen = cancellable(child_->executeChunkTraced());

    auto dir = Direction::OUT;
    if (direction_ == cypher::RelationshipDirection::RIGHT_TO_LEFT) {
        dir = Direction::IN;
    } else if (direction_ == cypher::RelationshipDirection::UNDIRECTED) {
        dir = Direction::BOTH;
    }

    std::unordered_set<VertexId> src_allowed;
    std::unordered_set<VertexId> dst_allowed;
    auto loadAllowed = [&](uint32_t index_id, const PropertyValue& value,
                           std::unordered_set<VertexId>& out) -> folly::coro::Task<void> {
        if (index_id == 0)
            co_return;
        std::vector<PropertyValue> one{value};
        std::vector<VertexId> scan_batch;
        auto gen = cancellable(store_.scanVerticesByIndexId(index_id, one, scan_batch));
        while (auto batch = co_await gen.next()) {
            for (VertexId vid : *batch)
                out.insert(vid);
        }
    };
    if (src_filter_index_id_ != 0)
        co_await loadAllowed(src_filter_index_id_, src_filter_value_, src_allowed);
    if (dst_filter_index_id_ != 0)
        co_await loadAllowed(dst_filter_index_id_, dst_filter_value_, dst_allowed);

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

    auto loadEdge = [&](EdgeId eid, EdgeLabelId elid, VertexId src, VertexId dst) -> folly::coro::Task<EdgeValue> {
        EdgeValue ev;
        ev.id = eid;
        ev.src_id = src;
        ev.dst_id = dst;
        ev.label_id = elid;
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
        bool physical_out = true; // true: found via OUT adjacency
    };

    std::unordered_map<VertexId, std::vector<DirectedEdgeEntry>> adjacency_cache;
    std::unordered_map<VertexId, bool> dst_label_cache;

    auto scanDirected = [&](VertexId vid, Direction scan_dir) -> folly::coro::Task<std::vector<DirectedEdgeEntry>> {
        std::vector<DirectedEdgeEntry> out;
        bool phy_out = (scan_dir == Direction::OUT);
        std::vector<ISyncGraphDataStore::EdgeIndexEntry> scan_batch;
        for (const auto& label_filter : scan_filters) {
            auto edge_gen = cancellable(store_.scanEdges(vid, scan_dir, label_filter, scan_batch));
            while (auto edge_batch = co_await edge_gen.next()) {
                for (const auto& e : *edge_batch)
                    out.push_back({e.neighbor_id, e.edge_id, e.edge_label_id, phy_out});
            }
        }
        co_return out;
    };

    auto labelsAllowed = [&](const LabelIdSet& labels) {
        for (LabelId need : dst_label_ids_) {
            if (labels.find(need) == labels.end())
                return false;
        }
        return true;
    };

    auto hasDstLabels = [&](VertexId vid) -> folly::coro::Task<bool> {
        if (dst_label_ids_.empty())
            co_return true;
        auto it = dst_label_cache.find(vid);
        if (it != dst_label_cache.end())
            co_return it->second;
        auto labels = co_await store_.getVertexLabels(vid);
        const bool ok = labelsAllowed(labels);
        dst_label_cache.emplace(vid, ok);
        co_return ok;
    };

    // A VLE frontier can have thousands of neighbors. Checking labels one
    // neighbor at a time costs one storage dispatch per vertex (complex-12:
    // ~47k dispatches). Resolve the whole adjacency list in bounded batches
    // and let the existing per-vertex cache serve the DFS checks.
    auto preloadDstLabels = [&](const std::vector<VertexId>& vids) -> folly::coro::Task<void> {
        if (dst_label_ids_.empty() || vids.empty())
            co_return;
        constexpr size_t kLabelBatch = 1024;
        std::vector<VertexId> missing;
        std::unordered_set<VertexId> seen;
        missing.reserve(std::min(vids.size(), kLabelBatch));
        seen.reserve(vids.size());
        for (VertexId vid : vids) {
            if (dst_label_cache.find(vid) == dst_label_cache.end() && seen.insert(vid).second)
                missing.push_back(vid);
            if (missing.size() == kLabelBatch) {
                auto labels = co_await store_.getVertexLabelsBatch(missing);
                for (size_t i = 0; i < missing.size(); ++i)
                    dst_label_cache.emplace(missing[i], labelsAllowed(labels[i]));
                missing.clear();
            }
        }
        if (!missing.empty()) {
            auto labels = co_await store_.getVertexLabelsBatch(missing);
            for (size_t i = 0; i < missing.size(); ++i)
                dst_label_cache.emplace(missing[i], labelsAllowed(labels[i]));
        }
    };

    auto scanAll = [&](VertexId vid) -> folly::coro::Task<std::vector<DirectedEdgeEntry>> {
        auto cache_it = adjacency_cache.find(vid);
        if (cache_it != adjacency_cache.end())
            co_return cache_it->second;
        std::vector<DirectedEdgeEntry> result;
        if (split_undirected) {
            auto out = co_await scanDirected(vid, Direction::OUT);
            auto in = co_await scanDirected(vid, Direction::IN);
            out.insert(out.end(), std::make_move_iterator(in.begin()), std::make_move_iterator(in.end()));
            result = std::move(out);
        } else {
            result = co_await scanDirected(vid, dir);
        }
        std::vector<VertexId> neighbors;
        neighbors.reserve(result.size());
        for (const auto& edge : result)
            neighbors.push_back(edge.neighbor_id);
        co_await preloadDstLabels(neighbors);
        adjacency_cache.emplace(vid, result);
        co_return result;
    };

    // Resolve all children of one frontier vertex with one scanEdgesBatch per
    // label instead of one scanEdges dispatch per child. The DFS order and the
    // per-source edge order are unchanged; this only warms adjacency_cache.
    auto preloadAdjacency = [&](const std::vector<DirectedEdgeEntry>& edges) -> folly::coro::Task<void> {
        std::vector<VertexId> missing;
        std::unordered_set<VertexId> seen;
        missing.reserve(edges.size());
        seen.reserve(edges.size());
        for (const auto& edge : edges) {
            if (adjacency_cache.find(edge.neighbor_id) == adjacency_cache.end() &&
                seen.insert(edge.neighbor_id).second) {
                missing.push_back(edge.neighbor_id);
                adjacency_cache.emplace(edge.neighbor_id, std::vector<DirectedEdgeEntry>{});
            }
        }
        if (missing.empty())
            co_return;

        auto scan_into_cache = [&](Direction scan_dir, bool phy_out) -> folly::coro::Task<void> {
            for (const auto& label_filter : scan_filters) {
                auto batch_gen = cancellable(store_.scanEdgesBatch(missing, scan_dir, label_filter));
                while (auto batch = co_await batch_gen.next()) {
                    for (const auto& be : *batch) {
                        auto it = adjacency_cache.find(be.src_id);
                        if (it != adjacency_cache.end())
                            it->second.push_back(
                                {be.entry.neighbor_id, be.entry.edge_id, be.entry.edge_label_id, phy_out});
                    }
                }
            }
        };
        if (split_undirected) {
            co_await scan_into_cache(Direction::OUT, true);
            co_await scan_into_cache(Direction::IN, false);
        } else {
            co_await scan_into_cache(dir, dir == Direction::OUT);
        }

        std::vector<VertexId> neighbors;
        for (VertexId vid : missing) {
            auto it = adjacency_cache.find(vid);
            if (it == adjacency_cache.end())
                continue;
            for (const auto& edge : it->second)
                neighbors.push_back(edge.neighbor_id);
        }
        co_await preloadDstLabels(neighbors);
    };

    while (auto chunk = co_await child_gen.next()) {
        const size_t input_rows = chunk->numRows();

        // Evaluate inline edge-property expressions once per input row.  The
        // filter values may depend on outer variables (e.g. {score: a.name}),
        // so they cannot be precomputed at bind time.
        std::unordered_map<const binder::BoundExpression*, std::vector<Value>> filter_values;
        if (!edge_prop_filter_exprs_.empty()) {
            ExpressionEvaluator evaluator(eval_ctx_);
            std::vector<const binder::BoundExpression*> exprs;
            std::unordered_set<const binder::BoundExpression*> seen;
            for (const auto& [label_id, filters] : edge_prop_filter_exprs_) {
                (void)label_id;
                for (const auto& [prop_id, expr] : filters) {
                    (void)prop_id;
                    if (expr && seen.insert(expr.get()).second)
                        exprs.push_back(expr.get());
                }
            }
            evaluator.resetForChunk();
            co_await evaluator.prefetch(exprs, *chunk);
            for (const auto* expr : exprs) {
                Column values = Column::flat(binder::BoundTypeKind::ANY, input_rows);
                evaluator.evaluate(*expr, *chunk, values);
                auto& out = filter_values[expr];
                out.reserve(input_rows);
                for (size_t i = 0; i < input_rows; ++i)
                    out.push_back(values.getValue(i));
            }
        }
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
            // Physical src/dst of the incoming edge so loadEdge gets the
            // correct direction regardless of traversal direction.
            VertexId incoming_physical_src = INVALID_VERTEX_ID;
            VertexId incoming_physical_dst = INVALID_VERTEX_ID;
        };

        for (size_t src_row = 0; src_row < input_rows; ++src_row) {
            // Heaviest loop in the engine: a single input row can expand into a whole
            // traversal, so check here as well as at the store batch boundaries.
            if (cancelled())
                co_return;
            std::vector<EdgeId> expected_edge_ids;
            if (edge_list_bound_) {
                if (edge_list_col_idx_ < 0 || static_cast<size_t>(edge_list_col_idx_) >= input_cols)
                    continue;
                expected_edge_ids = edgeIdsFromValue(chunk->columns[edge_list_col_idx_].getValue(src_row));
            }

            VertexId src_id = INVALID_VERTEX_ID;
            if (src_col_idx_ >= 0 && static_cast<size_t>(src_col_idx_) < input_cols) {
                const Value& val = chunk->columns[src_col_idx_].getValue(src_row);
                if (std::holds_alternative<VertexValuePtr>(val)) {
                    src_id = (*std::get<VertexValuePtr>(val)).id;
                } else if (std::holds_alternative<VertexRef>(val)) {
                    src_id = std::get<VertexRef>(val).id;
                } else if (std::holds_alternative<int64_t>(val)) {
                    src_id = static_cast<VertexId>(std::get<int64_t>(val));
                }
            }
            if (src_id == INVALID_VERTEX_ID)
                continue;

            EdgeId prev_edge_id = INVALID_EDGE_ID;
            if (prev_edge_col_idx_ >= 0 && static_cast<size_t>(prev_edge_col_idx_) < input_cols) {
                prev_edge_id = edgeIdFromValue(chunk->columns[prev_edge_col_idx_].getValue(src_row));
            }

            const bool src_matches = (src_filter_index_id_ == 0 || src_allowed.count(src_id) != 0);
            const bool dst_filter_active = (dst_filter_index_id_ != 0);
            auto dst_matches = [&](VertexId vid) { return !dst_filter_active || dst_allowed.count(vid) != 0; };

            // OR pruning: a non-matching source can only produce rows through
            // the dst side. If the dst index is empty, it cannot produce rows.
            if (!src_matches && dst_filter_active && dst_allowed.empty())
                continue;

            // Collect initial edges from source vertex
            std::vector<DirectedEdgeEntry> start_edges = co_await scanAll(src_id);
            if (max_hops_ < 0 || max_hops_ > 1)
                co_await preloadAdjacency(start_edges);
            if (prev_edge_id != INVALID_EDGE_ID) {
                start_edges.erase(std::remove_if(start_edges.begin(), start_edges.end(),
                                                 [&](const DirectedEdgeEntry& e) { return e.edge_id == prev_edge_id; }),
                                  start_edges.end());
            }

            // Emit identity path when min_hops == 0 (zero-hop: src == dst)
            if (min_hops_ == 0 && co_await hasDstLabels(src_id)) {
                bool emit_identity = true;
                if (dst_bound_) {
                    VertexId bound_dst = vertexIdFromValue(chunk->columns[dst_col_idx_].getValue(src_row));
                    if (bound_dst != src_id)
                        emit_identity = false;
                }
                if (edge_list_bound_ && !expected_edge_ids.empty())
                    emit_identity = false;
                if (emit_identity && (src_matches || dst_matches(src_id))) {
                    OutputEntry identity_entry;
                    identity_entry.src_row = src_row;
                    identity_entry.dst_id = src_id;
                    if (!path_var_.empty()) {
                        PathValue pv;
                        const Value& src_val = chunk->columns[src_col_idx_].getValue(src_row);
                        if (std::holds_alternative<VertexValuePtr>(src_val)) {
                            pv.elements.push_back(ValueStorage{src_val});
                        } else {
                            pv.elements.push_back(ValueStorage{Value(mk<VertexValue>(co_await loadVertex(src_id)))});
                        }
                        identity_entry.path = std::move(pv);
                    }
                    if (!edge_var_.empty() && !edge_list_bound_) {
                        identity_entry.edge_list = ListValue{};
                    }
                    output_buffer.push_back(std::move(identity_entry));
                }
            }

            // If only zero-hop requested, skip edge scanning and DFS
            if (max_hops_ == 0)
                continue;

            if (start_edges.empty())
                continue;

            std::unordered_set<EdgeVisitKey, EdgeVisitKeyHash> visited_edges;
            if (prev_edge_id != INVALID_EDGE_ID)
                visited_edges.insert(EdgeVisitKey{prev_edge_id});

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
                if (!edge_prop_filter_exprs_.empty()) {
                    auto fit = edge_prop_filter_exprs_.find(edge.edge_label_id);
                    if (fit != edge_prop_filter_exprs_.end() && !fit->second.empty()) {
                        std::vector<uint16_t> prop_ids;
                        for (const auto& [pid, _] : fit->second)
                            prop_ids.push_back(pid);
                        auto props = co_await store_.getEdgeProperties(edge.edge_label_id, edge.edge_id, prop_ids);
                        bool pass = true;
                        if (props) {
                            for (const auto& [pid, expr] : fit->second) {
                                auto vit = filter_values.find(expr.get());
                                if (vit != filter_values.end() && static_cast<size_t>(src_row) < vit->second.size()) {
                                    auto expected = valueToPropertyValue(vit->second[src_row]);
                                }
                                if (!expr || vit == filter_values.end() ||
                                    static_cast<size_t>(src_row) >= vit->second.size() || pid >= props->size() ||
                                    !props->at(pid).has_value() ||
                                    props->at(pid).value() != valueToPropertyValue(vit->second[src_row])) {
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

                if (next_depth >= min_hops_ && (src_matches || dst_matches(edge.neighbor_id)) &&
                    co_await hasDstLabels(edge.neighbor_id)) {
                    OutputEntry entry;
                    entry.src_row = src_row;
                    entry.dst_id = edge.neighbor_id;
                    if (!path_var_.empty()) {
                        // Build path: stack vertices/edges + current edge + destination
                        PathValue pv;
                        const Value& src_val = chunk->columns[src_col_idx_].getValue(src_row);
                        if (std::holds_alternative<VertexValuePtr>(src_val)) {
                            pv.elements.push_back(ValueStorage{src_val});
                        } else {
                            pv.elements.push_back(ValueStorage{Value(mk<VertexValue>(co_await loadVertex(src_id)))});
                        }
                        for (size_t si = 1; si < stack.size(); ++si) {
                            pv.elements.push_back(ValueStorage{Value(mk<EdgeValue>(
                                co_await loadEdge(stack[si].incoming_edge_id, stack[si].incoming_edge_label_id,
                                                  stack[si].incoming_physical_src, stack[si].incoming_physical_dst)))});
                            pv.elements.push_back(
                                ValueStorage{Value(mk<VertexValue>(co_await loadVertex(stack[si].vertex)))});
                        }
                        // Add current edge and destination vertex
                        VertexId cur_phys_src = edge.physical_out ? frame.vertex : edge.neighbor_id;
                        VertexId cur_phys_dst = edge.physical_out ? edge.neighbor_id : frame.vertex;
                        pv.elements.push_back(ValueStorage{Value(mk<EdgeValue>(
                            co_await loadEdge(edge.edge_id, edge.edge_label_id, cur_phys_src, cur_phys_dst)))});
                        VertexValue dst_vv = co_await loadVertex(edge.neighbor_id);
                        pv.elements.push_back(ValueStorage{Value(mk<VertexValue>(std::move(dst_vv)))});
                        entry.path = std::move(pv);
                    }
                    // P2: build edge list for named edge variable
                    if (!edge_var_.empty() && !edge_list_bound_) {
                        ListValue lv;
                        for (size_t si = 1; si < stack.size(); ++si) {
                            lv.elements.push_back(ValueStorage{Value(mk<EdgeValue>(
                                co_await loadEdge(stack[si].incoming_edge_id, stack[si].incoming_edge_label_id,
                                                  stack[si].incoming_physical_src, stack[si].incoming_physical_dst)))});
                        }
                        VertexId el_phys_src = edge.physical_out ? frame.vertex : edge.neighbor_id;
                        VertexId el_phys_dst = edge.physical_out ? edge.neighbor_id : frame.vertex;
                        lv.elements.push_back(ValueStorage{Value(mk<EdgeValue>(
                            co_await loadEdge(edge.edge_id, edge.edge_label_id, el_phys_src, el_phys_dst)))});
                        entry.edge_list = std::move(lv);
                    }
                    bool emit = true;
                    if (dst_bound_) {
                        VertexId bound_dst = vertexIdFromValue(chunk->columns[dst_col_idx_].getValue(src_row));
                        if (bound_dst != edge.neighbor_id)
                            emit = false;
                    }
                    if (edge_list_bound_) {
                        std::vector<EdgeId> path_ids;
                        path_ids.reserve(stack.size());
                        for (size_t si = 1; si < stack.size(); ++si)
                            path_ids.push_back(stack[si].incoming_edge_id);
                        path_ids.push_back(edge.edge_id);
                        bool equal = path_ids.size() == expected_edge_ids.size();
                        if (equal) {
                            for (size_t pi = 0; pi < path_ids.size(); ++pi) {
                                if (path_ids[pi] != expected_edge_ids[pi]) {
                                    equal = false;
                                    break;
                                }
                            }
                        }
                        if (!equal)
                            emit = false;
                    }
                    if (emit)
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
                                output.setValue(path_col_idx, i,
                                                Value(mk<PathValue>(std::move(output_buffer[i].path))));
                            }
                        }
                        // Fill edge list column if present
                        if (!edge_var_.empty() && !edge_list_bound_) {
                            size_t edge_col_idx = input_cols + (dst_bound_ ? 0 : 1) + (path_var_.empty() ? 0 : 1);
                            for (size_t i = 0; i < output_buffer.size(); ++i) {
                                output.setValue(edge_col_idx, i,
                                                Value(mk<ListValue>(std::move(output_buffer[i].edge_list))));
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
                    if (max_hops_ < 0 || next_depth + 1 < max_hops_)
                        co_await preloadAdjacency(next_edges);

                    VertexId push_physical_src = edge.physical_out ? frame.vertex : edge.neighbor_id;
                    VertexId push_physical_dst = edge.physical_out ? edge.neighbor_id : frame.vertex;
                    stack.push_back({edge.neighbor_id, next_depth, std::move(next_edges), 0, edge_key, true,
                                     edge.edge_id, edge.edge_label_id, push_physical_src, push_physical_dst});
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
                    output.setValue(path_col_idx, i, Value(mk<PathValue>(std::move(output_buffer[i].path))));
                }
            }
            // Fill edge list column if present
            if (!edge_var_.empty() && !edge_list_bound_) {
                size_t edge_col_idx = input_cols + (dst_bound_ ? 0 : 1) + (path_var_.empty() ? 0 : 1);
                for (size_t i = 0; i < output_buffer.size(); ++i) {
                    output.setValue(edge_col_idx, i, Value(mk<ListValue>(std::move(output_buffer[i].edge_list))));
                }
            }
            output.count = output_buffer.size();
            co_yield std::move(output);
        }
    }
}

} // namespace compute
} // namespace eugraph
