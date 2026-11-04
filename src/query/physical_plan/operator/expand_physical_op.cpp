#include "query/physical_plan/operator/expand_physical_op.hpp"

#include "common/types/graph_types.hpp"

#include <algorithm>
#include <cstdlib>

#include "query/dataset/cell_access.hpp"
#include "query/dataset/row.hpp"
#include <array>

namespace eugraph {
namespace compute {

std::atomic<uint64_t> ExpandPhysicalOp::cache_hit_count_{0};

uint64_t ExpandPhysicalOp::cacheHitCount() noexcept {
    return cache_hit_count_.load(std::memory_order_relaxed);
}

void ExpandPhysicalOp::resetCacheHitCount() noexcept {
    cache_hit_count_.store(0, std::memory_order_relaxed);
}

namespace {

EdgeId edgeIdFromValue(const Value& val) {
    if (std::holds_alternative<EdgeValuePtr>(val))
        return (*std::get<EdgeValuePtr>(val)).id;
    if (std::holds_alternative<EdgeKey>(val))
        return std::get<EdgeKey>(val).id;
    return INVALID_EDGE_ID;
}

} // anonymous namespace

namespace {

constexpr size_t kDefaultExpandInputRows = 128;

/// Bound how many input rows one Expand call may process at once. This is the
/// main defence against a single wildly expanding hop materialising tens of
/// millions of rows inside one DataChunk; it is tunable for experiments.
size_t expandInputRows() {
    const char* env = std::getenv("EUGRAPH_EXPAND_INPUT_ROWS");
    if (env == nullptr || *env == '\0')
        return kDefaultExpandInputRows;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(env, &end, 10);
    if (end == env || end == nullptr || *end != '\0' || parsed == 0)
        return kDefaultExpandInputRows;
    return static_cast<size_t>(std::min<unsigned long>(parsed, 4096));
}

/// Copy a logical row range into a standalone flat chunk. This deliberately
/// materialises only the bounded input slice; it preserves values and NULLs
/// without depending on the source chunk's selection vector.
DataChunk sliceChunk(const DataChunk& chunk, size_t offset, size_t count) {
    DataChunk out;
    out.count = count;
    out.columns.reserve(chunk.columns.size());
    for (const auto& src : chunk.columns) {
        Column dst = Column::flat(src.type, count);
        for (size_t i = 0; i < count; ++i) {
            if (src.isNull(offset + i))
                dst.setNull(i);
            else
                dst.setValue(i, src.getValue(offset + i));
        }
        out.columns.push_back(std::move(dst));
    }
    out.sel = SelectionVector::identity(count);
    return out;
}

} // namespace

folly::coro::AsyncGenerator<DataChunk> ExpandPhysicalOp::slicedChild() {
    auto gen = child_->executeChunkTraced();
    const size_t batch_rows = expandInputRows();
    while (auto chunk = co_await gen.next()) {
        const size_t rows = chunk->numRows();
        if (rows == 0)
            continue;
        if (rows <= batch_rows) {
            co_yield std::move(*chunk);
            continue;
        }
        for (size_t offset = 0; offset < rows; offset += batch_rows)
            co_yield sliceChunk(*chunk, offset, std::min(batch_rows, rows - offset));
    }
}

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
    if (full_edge_scan_) {
        size_t remaining_rows = limit_hint_.value_or(std::numeric_limits<size_t>::max());
        for (EdgeLabelId lid : full_scan_labels_) {
            if (remaining_rows == 0)
                break;
            std::vector<ISyncGraphDataStore::EdgeTypeIndexEntry> edge_entries;
            auto edge_gen = cancellable(store_.scanEdgesByType(lid, std::nullopt, std::nullopt, edge_entries));
            while (remaining_rows > 0) {
                auto edge_batch = co_await edge_gen.next();
                if (!edge_batch.has_value())
                    break;
                const size_t n = std::min(edge_batch->size(), remaining_rows);
                DataChunk out;
                out.count = n;
                out.addColumn(output_types_[0].kind);             // src vertex reference
                out.addColumn(binder::BoundTypeKind::EDGE_KEY);   // edge
                out.addColumn(binder::BoundTypeKind::VERTEX_REF); // dst
                out.columns[0].reserve(n);
                out.columns[1].reserve(n);
                out.columns[2].reserve(n);
                auto* src_data = out.columns[0].buffer->vertex_ref_data.data();
                auto* edge_data = out.columns[1].buffer->edge_key_data.data();
                auto* dst_data = out.columns[2].buffer->vertex_ref_data.data();
                for (size_t i = 0; i < n; ++i) {
                    const auto& e = (*edge_batch)[i];
                    src_data[i] = VertexRef(e.src_vertex_id);
                    edge_data[i] = EdgeKey(e.edge_id, e.src_vertex_id, e.dst_vertex_id, lid, e.seq);
                    dst_data[i] = VertexRef(e.dst_vertex_id);
                }
                if (direction_ == cypher::RelationshipDirection::UNDIRECTED)
                    remaining_rows = (remaining_rows > n) ? remaining_rows - n : 0;
                else
                    remaining_rows = (remaining_rows > n) ? remaining_rows - n : 0;
                co_yield std::move(out);

                if (remaining_rows == 0)
                    break;

                if (direction_ != cypher::RelationshipDirection::UNDIRECTED)
                    continue;

                // UNDIRECTED semantics: an asymmetric edge matches once from
                // each endpoint. The type index stores the physical direction
                // only, so emit a reversed copy for every non-self edge.
                size_t reverse_count = 0;
                for (size_t i = 0; i < n; ++i) {
                    if ((*edge_batch)[i].src_vertex_id != (*edge_batch)[i].dst_vertex_id)
                        ++reverse_count;
                }
                if (reverse_count == 0) {
                    if (remaining_rows == 0)
                        break;
                    continue;
                }

                DataChunk reverse;
                reverse.count = reverse_count;
                reverse.addColumn(output_types_[0].kind);
                reverse.addColumn(binder::BoundTypeKind::EDGE_KEY);
                reverse.addColumn(binder::BoundTypeKind::VERTEX_REF);
                reverse.columns[0].reserve(reverse_count);
                reverse.columns[1].reserve(reverse_count);
                reverse.columns[2].reserve(reverse_count);
                auto* rsrc_data = reverse.columns[0].buffer->vertex_ref_data.data();
                auto* redge_data = reverse.columns[1].buffer->edge_key_data.data();
                auto* rdst_data = reverse.columns[2].buffer->vertex_ref_data.data();
                size_t w = 0;
                for (size_t i = 0; i < n; ++i) {
                    const auto& e = (*edge_batch)[i];
                    if (e.src_vertex_id == e.dst_vertex_id)
                        continue;
                    rsrc_data[w] = VertexRef(e.dst_vertex_id);
                    redge_data[w] = EdgeKey(e.edge_id, e.dst_vertex_id, e.src_vertex_id, lid, e.seq);
                    rdst_data[w] = VertexRef(e.src_vertex_id);
                    ++w;
                }
                remaining_rows = (remaining_rows > reverse_count) ? remaining_rows - reverse_count : 0;
                co_yield std::move(reverse);
                if (remaining_rows == 0)
                    break;
            }
        }
        co_return;
    }

    const bool allowed_filter = allowed_dst_index_id_ != 0 && allowed_edge_label_ != INVALID_EDGE_LABEL_ID &&
                                allowed_dst_values_ && !allowed_dst_values_->empty();
    std::vector<VertexId> allowed_dst_vids;
    if (allowed_filter) {
        std::vector<VertexId> allowed_batch;
        for (const auto& value : *allowed_dst_values_) {
            std::vector<PropertyValue> one{value};
            auto gen = cancellable(store_.scanVerticesByIndexId(allowed_dst_index_id_, one, allowed_batch));
            while (auto batch = co_await gen.next()) {
                for (VertexId vid : *batch)
                    allowed_dst_vids.push_back(vid);
            }
        }
    }

    auto child_gen = cancellable(slicedChild());

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

    // Reused across chunks: clearing keeps the capacity, so the per-chunk heap
    // traffic for edge accumulation drops to (near) zero. `edges` grows to the
    // chunk's expansion count, which is the hot allocation otherwise.
    std::vector<EdgeEntryScratch> edges;
    std::unordered_set<EdgeId> allowed_seen;
    // Per-input-row source ids, resolved once per chunk and reused by both the
    // batched and per-row paths (the output EdgeKey also needs them).
    std::vector<VertexId> row_src_ids;

    while (auto chunk = co_await child_gen.next()) {
        const size_t input_rows = chunk->numRows();
        const size_t input_cols = chunk->numColumns();

        row_src_ids.assign(input_rows, INVALID_VERTEX_ID);
        for (size_t src_row = 0; src_row < input_rows; ++src_row) {
            VertexId src_id = INVALID_VERTEX_ID;
            if (src_col_idx_ >= 0 && static_cast<size_t>(src_col_idx_) < input_cols) {
                const Value& val = chunk->columns[src_col_idx_].getValue(src_row);
                if (std::holds_alternative<VertexValuePtr>(val))
                    src_id = (*std::get<VertexValuePtr>(val)).id;
                else if (std::holds_alternative<VertexRef>(val))
                    src_id = std::get<VertexRef>(val).id;
                else if (std::holds_alternative<int64_t>(val))
                    src_id = static_cast<VertexId>(std::get<int64_t>(val));
            }
            row_src_ids[src_row] = src_id;
        }

        // Reset per chunk; capacity is retained (see the hoisted declarations).
        edges.clear();
        allowed_seen.clear();

        auto scanOneDirection = [&](VertexId src_id, size_t src_row, Direction scan_dir) -> folly::coro::Task<void> {
            if (allowed_filter) {
                for (VertexId dst_id : allowed_dst_vids) {
                    std::vector<ISyncGraphDataStore::EdgeTypeIndexEntry> out_entries;
                    auto out_gen =
                        cancellable(store_.scanEdgesByType(allowed_edge_label_, src_id, dst_id, out_entries));
                    while (auto out_batch = co_await out_gen.next()) {
                        for (const auto& entry : *out_batch) {
                            if (allowed_seen.insert(entry.edge_id).second)
                                edges.push_back({src_row, dst_id, entry.edge_id, allowed_edge_label_, entry.seq, true});
                            break;
                        }
                        break;
                    }
                    std::vector<ISyncGraphDataStore::EdgeTypeIndexEntry> in_entries;
                    auto in_gen = cancellable(store_.scanEdgesByType(allowed_edge_label_, dst_id, src_id, in_entries));
                    while (auto in_batch = co_await in_gen.next()) {
                        for (const auto& entry : *in_batch) {
                            if (entry.src_vertex_id == entry.dst_vertex_id)
                                continue;
                            if (allowed_seen.insert(entry.edge_id).second)
                                edges.push_back(
                                    {src_row, dst_id, entry.edge_id, allowed_edge_label_, entry.seq, false});
                            break;
                        }
                        break;
                    }
                }
                co_return;
            }
            if (dst_bound_ && !edge_bound_ && !scan_filters.empty() &&
                std::all_of(scan_filters.begin(), scan_filters.end(),
                            [](const std::optional<EdgeLabelId>& f) { return f.has_value(); })) {
                // Correlated chain endpoint: the destination is already known,
                // so seek the exact `{src}{dir}{label}{neighbor}` entry instead
                // of scanning every edge of the source and filtering it out.
                const VertexId bound = columnVertexId(chunk->columns[dst_col_idx_], src_row);
                if (bound == INVALID_VERTEX_ID)
                    co_return;
                for (const auto& label_filter : scan_filters) {
                    auto edge_gen =
                        cancellable(store_.scanEdgesExact(src_id, scan_dir, *label_filter, bound, edge_scratch_));
                    while (auto edge_batch = co_await edge_gen.next()) {
                        for (const auto& entry : *edge_batch) {
                            const bool phy_out = (scan_dir == Direction::OUT);
                            edges.push_back(
                                {src_row, entry.neighbor_id, entry.edge_id, entry.edge_label_id, entry.seq, phy_out});
                        }
                    }
                }
                co_return;
            }
            for (const auto& label_filter : scan_filters) {
                auto edge_gen = cancellable(store_.scanEdges(src_id, scan_dir, label_filter, edge_scratch_));
                while (auto edge_batch = co_await edge_gen.next()) {
                    for (const auto& entry : *edge_batch) {
                        if (edge_bound_) {
                            const Value& eval = chunk->columns[edge_col_idx_].getValue(src_row);
                            if (edgeIdFromValue(eval) != entry.edge_id)
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

        bool batched = false;
        bool labels_pre_filtered = false;
        if (!allowed_filter && !dst_bound_ && !edge_bound_) {
            std::vector<VertexId> src_ids;
            std::unordered_map<VertexId, std::vector<size_t>> rows_of_src;
            std::unordered_set<VertexId> seen_src;
            for (size_t src_row = 0; src_row < input_rows; ++src_row) {
                const VertexId src_id = row_src_ids[src_row];
                if (src_id != INVALID_VERTEX_ID) {
                    rows_of_src[src_id].push_back(src_row);
                    if (seen_src.insert(src_id).second)
                        src_ids.push_back(src_id);
                }
            }
            if (!src_ids.empty()) {
                const bool use_cache = neighbor_cache_enabled_ && !neighbor_cache_disabled_ && !split_undirected;
                if (!use_cache) {
                    for (const auto& label_filter : scan_filters) {
                        // UNDIRECTED scans OUT and IN separately so each EdgeKey
                        // keeps the stored physical direction.  Batched, the two
                        // passes interleave by source only after a stable sort.
                        const std::array<Direction, 2> dirs =
                            split_undirected ? std::array<Direction, 2>{Direction::OUT, Direction::IN}
                                             : std::array<Direction, 2>{dir, dir};
                        const size_t passes = split_undirected ? 2 : 1;
                        for (size_t pass = 0; pass < passes; ++pass) {
                            auto batch_gen = cancellable(store_.scanEdgesBatch(src_ids, dirs[pass], label_filter));
                            while (auto batch = co_await batch_gen.next()) {
                                for (const auto& be : *batch) {
                                    auto row_it = rows_of_src.find(be.src_id);
                                    if (row_it == rows_of_src.end())
                                        continue;
                                    const bool phy_out = (dirs[pass] == Direction::OUT);
                                    // Undirected OUT+IN dual pass: the same
                                    // self-loop appears in both adjacency
                                    // lists, so keep only the OUT one.  A
                                    // directed IN scan must keep it, otherwise
                                    // `(:A)<--()` misses an A->A edge.
                                    if (split_undirected && !phy_out && be.entry.neighbor_id == be.src_id)
                                        continue;
                                    for (size_t src_row : row_it->second)
                                        edges.push_back({src_row, be.entry.neighbor_id, be.entry.edge_id,
                                                         be.entry.edge_label_id, be.entry.seq, phy_out});
                                }
                            }
                        }
                    }
                    if (split_undirected && edges.size() > 1) {
                        std::stable_sort(
                            edges.begin(), edges.end(),
                            [](const EdgeEntryScratch& a, const EdgeEntryScratch& b) { return a.src_row < b.src_row; });
                    }
                } else {
                    // Correlated (PC) re-execution expands the same anchor once
                    // per path relationship. Scan only sources not yet cached
                    // and store entries after dst-label filtering, so a hit
                    // skips both the edge scan and the label probes. The cache
                    // is only enabled for read-only statements, so the snapshot
                    // cannot change under it.
                    std::vector<VertexId> miss_ids;
                    miss_ids.reserve(src_ids.size());
                    for (VertexId src_id : src_ids)
                        if (neighbor_cache_.find(src_id) == neighbor_cache_.end())
                            miss_ids.push_back(src_id);

                    std::unordered_map<VertexId, std::vector<CachedNeighbor>> fresh;
                    fresh.reserve(miss_ids.size());
                    for (VertexId src_id : miss_ids)
                        fresh.emplace(src_id, std::vector<CachedNeighbor>{});

                    if (!miss_ids.empty()) {
                        std::unordered_map<VertexId, size_t> miss_index;
                        miss_index.reserve(miss_ids.size() * 2);
                        for (size_t i = 0; i < miss_ids.size(); ++i)
                            miss_index.emplace(miss_ids[i], i);

                        std::vector<EdgeEntryScratch> miss_edges;
                        for (const auto& label_filter : scan_filters) {
                            auto batch_gen = cancellable(store_.scanEdgesBatch(miss_ids, dir, label_filter));
                            while (auto batch = co_await batch_gen.next()) {
                                for (const auto& be : *batch) {
                                    auto it = miss_index.find(be.src_id);
                                    if (it == miss_index.end())
                                        continue;
                                    miss_edges.push_back({it->second, be.entry.neighbor_id, be.entry.edge_id,
                                                          be.entry.edge_label_id, be.entry.seq, dir == Direction::OUT});
                                }
                            }
                        }
                        if (!dst_label_ids_.empty() && !miss_edges.empty())
                            co_await filterDstLabels(miss_edges);

                        for (const auto& e : miss_edges) {
                            VertexId src_id = miss_ids[e.src_row];
                            fresh[src_id].push_back({e.dst_id, e.edge_id, e.edge_label_id, e.seq, e.physical_out});
                        }

                        size_t fresh_edges = 0;
                        for (const auto& [_, list] : fresh)
                            fresh_edges += list.size();
                        if (neighbor_cache_.size() + fresh.size() > kMaxCachedSources ||
                            cached_edge_count_ + fresh_edges > kMaxCachedEdges) {
                            // Stop adding entries once the bound is reached, but
                            // keep what is already cached for later hits.
                            neighbor_cache_disabled_ = true;
                        } else {
                            cached_edge_count_ += fresh_edges;
                            for (auto& [src_id, list] : fresh)
                                neighbor_cache_.emplace(src_id, std::move(list));
                        }
                    }

                    // Reassemble in the same order as the uncached batched path:
                    // input source order, then per-source scan order, then rows.
                    for (VertexId src_id : src_ids) {
                        auto row_it = rows_of_src.find(src_id);
                        if (row_it == rows_of_src.end())
                            continue;
                        const std::vector<CachedNeighbor>* cached = nullptr;
                        auto hit = neighbor_cache_.find(src_id);
                        if (hit != neighbor_cache_.end()) {
                            cached = &hit->second;
                            cache_hit_count_.fetch_add(1, std::memory_order_relaxed);
                        } else {
                            auto f = fresh.find(src_id);
                            if (f != fresh.end())
                                cached = &f->second;
                        }
                        if (!cached)
                            continue;
                        for (const auto& e : *cached)
                            for (size_t src_row : row_it->second)
                                edges.push_back({src_row, e.dst_id, e.edge_id, e.edge_label_id, e.seq, e.physical_out});
                    }
                    labels_pre_filtered = true;
                }
            }
            batched = true;
        }

        if (!batched) {
            for (size_t src_row = 0; src_row < input_rows; ++src_row) {
                // One input row is one adjacency probe plus property loads, so this is
                // where a cancelled query should stop rather than finishing the chunk.
                if (cancelled())
                    co_return;
                const VertexId src_id = row_src_ids[src_row];
                if (src_id == INVALID_VERTEX_ID)
                    continue;

                if (allowed_filter) {
                    co_await scanOneDirection(src_id, src_row, Direction::OUT);
                    continue;
                }
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
                                               [&seen](const EdgeEntryScratch& e) {
                                                   if (seen.count(e.edge_id))
                                                       return true;
                                                   seen.insert(e.edge_id);
                                                   return false;
                                               }),
                                edges.end());
                } else {
                    if (allowed_filter) {
                        for (VertexId dst_id : allowed_dst_vids) {
                            std::vector<ISyncGraphDataStore::EdgeTypeIndexEntry> out_entries;
                            auto out_gen =
                                cancellable(store_.scanEdgesByType(allowed_edge_label_, src_id, dst_id, out_entries));
                            while (auto out_batch = co_await out_gen.next()) {
                                for (const auto& entry : *out_batch) {
                                    if (allowed_seen.insert(entry.edge_id).second)
                                        edges.push_back(
                                            {src_row, dst_id, entry.edge_id, allowed_edge_label_, entry.seq, true});
                                    break;
                                }
                                break;
                            }
                            if (dir == Direction::BOTH) {
                                std::vector<ISyncGraphDataStore::EdgeTypeIndexEntry> in_entries;
                                auto in_gen = cancellable(
                                    store_.scanEdgesByType(allowed_edge_label_, dst_id, src_id, in_entries));
                                while (auto in_batch = co_await in_gen.next()) {
                                    for (const auto& entry : *in_batch) {
                                        if (entry.src_vertex_id == entry.dst_vertex_id)
                                            continue;
                                        if (allowed_seen.insert(entry.edge_id).second)
                                            edges.push_back({src_row, dst_id, entry.edge_id, allowed_edge_label_,
                                                             entry.seq, false});
                                        break;
                                    }
                                    break;
                                }
                            }
                        }
                        continue;
                    }
                    for (const auto& label_filter : scan_filters) {
                        auto edge_gen = cancellable(store_.scanEdges(src_id, dir, label_filter, edge_scratch_));
                        while (auto edge_batch = co_await edge_gen.next()) {
                            for (const auto& entry : *edge_batch) {
                                if (dst_bound_) {
                                    if (columnVertexId(chunk->columns[dst_col_idx_], src_row) != entry.neighbor_id)
                                        continue;
                                }
                                if (edge_bound_) {
                                    const Value& eval = chunk->columns[edge_col_idx_].getValue(src_row);
                                    if (edgeIdFromValue(eval) != entry.edge_id)
                                        continue;
                                }
                                bool phy_out = (dir == Direction::OUT);
                                edges.push_back({src_row, entry.neighbor_id, entry.edge_id, entry.edge_label_id,
                                                 entry.seq, phy_out});
                            }
                        }
                    }
                }
            }
        }

        // Apply destination label filters. For large neighbor sets, point
        // label lookups are the bottleneck (one B-tree seek per neighbor).
        // A sequential label-forward scan + membership check is much cheaper
        // for high-degree destinations such as LDBC Message. The neighbour
        // cache stores already-filtered entries, so it skips this pass.
        if (!labels_pre_filtered && !edges.empty() && !dst_label_ids_.empty())
            co_await filterDstLabels(edges);

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

        // Fill new columns directly into their typed buffers. This used to go
        // through Value + Column::setValue per edge, which dominated Expand on
        // c5/c9/c14 (100k+ rows); the topology columns are fixed-layout.
        const bool append_edge = !edge_bound_ && !edge_var_.empty();
        const bool append_dst = !dst_bound_ && !dst_var_.empty();
        const size_t edge_col_idx = input_cols;
        const size_t dst_col_idx = append_edge ? input_cols + 1 : input_cols;

        VertexRef* dst_data = nullptr;
        EdgeKey* edge_data = nullptr;
        if (append_dst)
            dst_data = output.columns[dst_col_idx].buffer->vertex_ref_data.data();
        if (append_edge)
            edge_data = output.columns[edge_col_idx].buffer->edge_key_data.data();

        for (size_t i = 0; i < edges.size(); ++i) {
            const auto& e = edges[i];
            if (append_dst)
                dst_data[i] = VertexRef{e.dst_id};
            if (append_edge) {
                const VertexId sid = row_src_ids[e.src_row];
                // EdgeKey.src_id / dst_id must reflect the physically stored
                // edge direction so path rendering (`<-[:T]-` vs `-[:T]->`)
                // is correct for undirected traversals. For Direction::IN
                // hits during an UNDIRECTED scan, the physical src is the
                // neighbor and the physical dst is the current vertex.
                const VertexId physical_src = e.physical_out ? sid : e.dst_id;
                const VertexId physical_dst = e.physical_out ? e.dst_id : sid;
                edge_data[i] =
                    EdgeKey{e.edge_id, physical_src, physical_dst, e.edge_label_id, static_cast<uint32_t>(e.seq)};
            }
        }
        output.count = edges.size();
        co_yield std::move(output);
    }
}

folly::coro::Task<void> ExpandPhysicalOp::filterDstLabels(std::vector<EdgeEntryScratch>& list) {
    if (list.empty() || dst_label_ids_.empty())
        co_return;

    if (list.size() < 4096) {
        std::vector<VertexId> label_neighbors;
        std::unordered_set<VertexId> label_neighbors_seen;
        std::unordered_map<VertexId, LabelIdSet> label_map;
        for (const auto& e : list)
            if (label_neighbors_seen.insert(e.dst_id).second)
                label_neighbors.push_back(e.dst_id);
        auto labels_batch = co_await store_.getVertexLabelsBatch(label_neighbors);
        for (size_t i = 0; i < label_neighbors.size() && i < labels_batch.size(); ++i)
            label_map[label_neighbors[i]] = std::move(labels_batch[i]);
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [&](const EdgeEntryScratch& e) {
                                      auto it = label_map.find(e.dst_id);
                                      if (it == label_map.end())
                                          return true;
                                      for (LabelId need : dst_label_ids_)
                                          if (it->second.find(need) == it->second.end())
                                              return true;
                                      return false;
                                  }),
                   list.end());
        co_return;
    }

    std::unordered_set<VertexId> neighbor_set;
    neighbor_set.reserve(list.size() * 2);
    for (const auto& e : list)
        neighbor_set.insert(e.dst_id);

    std::unordered_set<VertexId> label_matches;
    std::unordered_set<VertexId> label_members;
    bool first = true;
    std::vector<VertexId> label_batch;
    for (LabelId need : dst_label_ids_) {
        label_members.clear();
        label_members.reserve(neighbor_set.size());
        auto label_gen = cancellable(store_.scanVerticesByLabel(need, label_batch));
        while (auto batch = co_await label_gen.next()) {
            for (VertexId vid : *batch) {
                if (neighbor_set.find(vid) != neighbor_set.end())
                    label_members.insert(vid);
            }
        }
        if (first) {
            label_matches.swap(label_members);
            first = false;
        } else {
            for (auto it = label_matches.begin(); it != label_matches.end();) {
                if (!label_members.contains(*it))
                    it = label_matches.erase(it);
                else
                    ++it;
            }
        }
        if (label_matches.empty())
            break;
    }

    list.erase(std::remove_if(list.begin(), list.end(),
                              [&](const EdgeEntryScratch& e) { return !label_matches.contains(e.dst_id); }),
               list.end());
}

} // namespace compute
} // namespace eugraph
