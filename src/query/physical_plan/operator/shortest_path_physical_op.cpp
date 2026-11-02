#include "query/physical_plan/operator/shortest_path_physical_op.hpp"

#include "common/types/constants.hpp"
#include "query/dataset/row.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <stdexcept>
#include <unordered_set>

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

Direction scanDirection(cypher::RelationshipDirection dir) {
    switch (dir) {
    case cypher::RelationshipDirection::LEFT_TO_RIGHT:
        return Direction::OUT;
    case cypher::RelationshipDirection::RIGHT_TO_LEFT:
        return Direction::IN;
    case cypher::RelationshipDirection::UNDIRECTED:
        return Direction::BOTH;
    }
    return Direction::OUT;
}

Direction oppositeDirection(Direction dir) {
    if (dir == Direction::OUT)
        return Direction::IN;
    if (dir == Direction::IN)
        return Direction::OUT;
    return Direction::BOTH;
}

constexpr size_t MAX_PATHS = 100000;
// Bound the persisted adjacency cache (~64 MiB at 64 B per Neighbor). The
// cache is a speed optimization only; lists beyond this budget are still
// returned to the current BFS, just not stored for later rows.
constexpr size_t MAX_CACHED_NEIGHBORS = 1'000'000;
// Keep the output batch bounded by path elements too, not only by row count:
// 1024 long paths can otherwise be much larger than one chunk should be.
constexpr size_t MAX_PENDING_PATH_ELEMENTS = 262144;

} // namespace

ShortestPathPhysicalOp::ShortestPathPhysicalOp(
    std::string src_var, std::string dst_var, std::optional<std::vector<EdgeLabelId>> label_filters,
    cypher::RelationshipDirection direction, int64_t min_hops, int64_t max_hops, IAsyncGraphDataStore& store,
    Schema input_schema, std::unique_ptr<PhysicalOperator> child, std::string path_var, binder::SlotId path_slot,
    bool all_shortest_paths, std::vector<LabelId> dst_label_ids, bool dst_label_missing, uint32_t src_filter_index_id,
    PropertyValue src_filter_value, uint32_t dst_filter_index_id, PropertyValue dst_filter_value)
    : src_var_(std::move(src_var)), dst_var_(std::move(dst_var)), label_filters_(std::move(label_filters)),
      direction_(direction), forward_dir_(scanDirection(direction)), backward_dir_(oppositeDirection(forward_dir_)),
      min_hops_(min_hops), max_hops_(max_hops), store_(store), input_schema_(std::move(input_schema)),
      child_(std::move(child)), path_var_(std::move(path_var)), path_slot_(path_slot),
      all_shortest_paths_(all_shortest_paths), dst_label_ids_(std::move(dst_label_ids)),
      dst_label_missing_(dst_label_missing), src_filter_index_id_(src_filter_index_id),
      src_filter_value_(std::move(src_filter_value)), dst_filter_index_id_(dst_filter_index_id),
      dst_filter_value_(std::move(dst_filter_value)) {
    if (!label_filters_.has_value()) {
        scan_filters_.push_back(std::nullopt);
    } else {
        for (auto lid : *label_filters_)
            scan_filters_.push_back(lid);
    }
    for (size_t i = 0; i < input_schema_.size(); ++i) {
        if (input_schema_[i] == src_var_)
            src_col_idx_ = static_cast<int>(i);
        if (input_schema_[i] == dst_var_)
            dst_col_idx_ = static_cast<int>(i);
    }
}

std::string ShortestPathPhysicalOp::toString() const {
    std::string mode = all_shortest_paths_ ? "allShortestPaths" : "shortestPath";
    return mode + "(src=" + src_var_ + ", dst=" + dst_var_ + ", hops=[" + std::to_string(min_hops_) + ".." +
           std::to_string(max_hops_) + "])";
}

void ShortestPathPhysicalOp::deriveOutputLayout(const TupleSlotLayout& input_layout) {
    slot_layout_ = input_layout;
    if (!path_var_.empty() && path_slot_ != binder::INVALID_SLOT_ID)
        slot_layout_.append(path_slot_);
}

folly::coro::Task<std::vector<std::shared_ptr<const ShortestPathPhysicalOp::NeighborList>>>
ShortestPathPhysicalOp::scanBatch(const std::vector<VertexId>& vids, Direction scan_dir) {
    std::vector<std::shared_ptr<const NeighborList>> result(vids.size());
    std::vector<VertexId> missing;
    std::vector<size_t> missing_pos;
    std::unordered_map<VertexId, size_t> batch_index;
    missing.reserve(vids.size());
    missing_pos.reserve(vids.size());
    for (size_t i = 0; i < vids.size(); ++i) {
        const VertexId vid = vids[i];
        if (vid == INVALID_VERTEX_ID)
            continue;
        const uint64_t key = (static_cast<uint64_t>(vid) << 2) | static_cast<uint64_t>(scan_dir);
        if (auto it = edge_cache_.find(key); it != edge_cache_.end()) {
            result[i] = it->second;
            continue;
        }
        if (batch_index.find(vid) != batch_index.end())
            continue;
        batch_index.emplace(vid, missing.size());
        missing.push_back(vid);
        missing_pos.push_back(i);
    }
    if (missing.empty())
        co_return result;

    std::vector<NeighborList> lists(missing.size());
    auto append_dir = [&](Direction dir) -> folly::coro::Task<void> {
        for (const auto& label_filter : scan_filters_) {
            auto gen = cancellable(store_.scanEdgesBatch(missing, dir, label_filter));
            while (auto batch = co_await gen.next()) {
                for (const auto& be : *batch) {
                    auto it = batch_index.find(be.src_id);
                    if (it == batch_index.end())
                        continue;
                    Neighbor n;
                    n.id = be.entry.neighbor_id;
                    EdgeLink link;
                    link.from = be.src_id;
                    link.to = n.id;
                    if (dir == Direction::OUT) {
                        link.src = be.src_id;
                        link.dst = n.id;
                    } else {
                        link.src = n.id;
                        link.dst = be.src_id;
                    }
                    link.edge_id = be.entry.edge_id;
                    link.edge_label_id = be.entry.edge_label_id;
                    link.seq = be.entry.seq;
                    n.link = link;
                    lists[it->second].push_back(n);
                }
            }
        }
        co_return;
    };
    if (scan_dir == Direction::BOTH) {
        co_await append_dir(Direction::OUT);
        co_await append_dir(Direction::IN);
    } else {
        co_await append_dir(scan_dir);
    }

    for (size_t j = 0; j < missing.size(); ++j) {
        auto list = std::make_shared<NeighborList>(std::move(lists[j]));
        const VertexId vid = missing[j];
        const uint64_t key = (static_cast<uint64_t>(vid) << 2) | static_cast<uint64_t>(scan_dir);
        if (cached_neighbors_ + list->size() <= MAX_CACHED_NEIGHBORS) {
            cached_neighbors_ += list->size();
            edge_cache_.emplace(key, list);
        }
        result[missing_pos[j]] = std::move(list);
    }
    // Duplicate frontier entries (normally impossible) share the same list.
    for (size_t i = 0; i < vids.size(); ++i) {
        if (result[i])
            continue;
        auto it = batch_index.find(vids[i]);
        if (it != batch_index.end())
            result[i] = result[missing_pos[it->second]];
    }
    co_return result;
}

folly::coro::Task<bool> ShortestPathPhysicalOp::hasDstLabels(VertexId vid) {
    if (dst_label_ids_.empty())
        co_return true;
    if (auto it = dst_label_cache_.find(vid); it != dst_label_cache_.end())
        co_return it->second;
    auto labels = co_await store_.getVertexLabels(vid);
    bool ok = true;
    for (LabelId need : dst_label_ids_) {
        if (labels.find(need) == labels.end()) {
            ok = false;
            break;
        }
    }
    dst_label_cache_.emplace(vid, ok);
    co_return ok;
}

PathValue ShortestPathPhysicalOp::buildPath(VertexId start, const std::vector<EdgeLink>& links) {
    PathValue path;
    VertexValue first;
    first.id = start;
    path.elements.push_back(ValueStorage{Value(mk<VertexValue>(std::move(first)))});
    for (const auto& link : links) {
        EdgeValue ev;
        ev.id = link.edge_id;
        ev.label_id = link.edge_label_id;
        ev.src_id = link.src;
        ev.dst_id = link.dst;
        ev.seq = link.seq;
        path.elements.push_back(ValueStorage{Value(mk<EdgeValue>(std::move(ev)))});
        VertexValue next;
        next.id = link.to;
        path.elements.push_back(ValueStorage{Value(mk<VertexValue>(std::move(next)))});
    }
    return path;
}

folly::coro::Task<std::optional<PathValue>> ShortestPathPhysicalOp::shortestBidirectional(VertexId start,
                                                                                          VertexId end) {
    if (start == end && min_hops_ <= 0)
        co_return buildPath(start, {});

    constexpr int INF = std::numeric_limits<int>::max();
    std::unordered_map<VertexId, Visit> f_visit;
    std::unordered_map<VertexId, Visit> b_visit;
    std::vector<VertexId> f_frontier{start};
    std::vector<VertexId> b_frontier{end};
    f_visit.emplace(start, Visit{0, {}, false});
    b_visit.emplace(end, Visit{0, {}, false});
    int f_depth = 0;
    int b_depth = 0;
    int best = INF;
    VertexId meet = INVALID_VERTEX_ID;

    while (!f_frontier.empty() || !b_frontier.empty()) {
        if (best != INF && f_depth + b_depth >= best)
            break;

        const bool expand_forward =
            !f_frontier.empty() && (b_frontier.empty() || f_frontier.size() <= b_frontier.size());
        const int next_depth = (expand_forward ? f_depth : b_depth) + 1;
        if (max_hops_ >= 0 && next_depth > max_hops_)
            break;
        if (best != INF && next_depth >= best)
            break;

        std::vector<VertexId> next;
        if (expand_forward) {
            auto forward_lists = co_await scanBatch(f_frontier, forward_dir_);
            for (size_t fi = 0; fi < f_frontier.size(); ++fi) {
                if (cancelled())
                    co_return std::nullopt;
                const VertexId v = f_frontier[fi];
                const auto& edges = *forward_lists[fi];
                for (const auto& e : edges) {
                    if (e.id == v)
                        continue;
                    auto [it, inserted] = f_visit.try_emplace(e.id, Visit{next_depth, {}, false});
                    if (inserted) {
                        it->second.parent = e.link;
                        it->second.has_parent = true;
                        next.push_back(e.id);
                    }
                    auto bit = b_visit.find(e.id);
                    if (bit == b_visit.end())
                        continue;
                    const int candidate = it->second.dist + bit->second.dist;
                    if (candidate < min_hops_ || (max_hops_ >= 0 && candidate > max_hops_))
                        continue;
                    if (candidate < best) {
                        best = candidate;
                        meet = e.id;
                    }
                }
            }
            f_frontier = std::move(next);
            f_depth = next_depth;
        } else {
            auto backward_lists = co_await scanBatch(b_frontier, backward_dir_);
            for (size_t bi = 0; bi < b_frontier.size(); ++bi) {
                if (cancelled())
                    co_return std::nullopt;
                const VertexId v = b_frontier[bi];
                const auto& edges = *backward_lists[bi];
                for (const auto& e : edges) {
                    if (e.id == v)
                        continue;
                    EdgeLink back;
                    back.from = e.id;
                    back.to = v;
                    back.src = e.link.src;
                    back.dst = e.link.dst;
                    back.edge_id = e.link.edge_id;
                    back.edge_label_id = e.link.edge_label_id;
                    back.seq = e.link.seq;
                    auto [it, inserted] = b_visit.try_emplace(e.id, Visit{next_depth, {}, false});
                    if (inserted) {
                        it->second.parent = back;
                        it->second.has_parent = true;
                        next.push_back(e.id);
                    }
                    auto fit = f_visit.find(e.id);
                    if (fit == f_visit.end())
                        continue;
                    const int candidate = fit->second.dist + it->second.dist;
                    if (candidate < min_hops_ || (max_hops_ >= 0 && candidate > max_hops_))
                        continue;
                    if (candidate < best) {
                        best = candidate;
                        meet = e.id;
                    }
                }
            }
            b_frontier = std::move(next);
            b_depth = next_depth;
        }
    }

    if (best == INF || meet == INVALID_VERTEX_ID)
        co_return std::nullopt;

    std::vector<EdgeLink> forward_links;
    VertexId cur = meet;
    while (cur != start) {
        auto it = f_visit.find(cur);
        if (it == f_visit.end() || !it->second.has_parent)
            co_return std::nullopt;
        forward_links.push_back(it->second.parent);
        cur = it->second.parent.from;
    }
    std::reverse(forward_links.begin(), forward_links.end());

    std::vector<EdgeLink> backward_links;
    cur = meet;
    while (cur != end) {
        auto it = b_visit.find(cur);
        if (it == b_visit.end() || !it->second.has_parent)
            co_return std::nullopt;
        backward_links.push_back(it->second.parent);
        cur = it->second.parent.to;
    }

    forward_links.insert(forward_links.end(), backward_links.begin(), backward_links.end());
    if (forward_links.size() != static_cast<size_t>(best))
        co_return std::nullopt;
    co_return buildPath(start, forward_links);
}

folly::coro::AsyncGenerator<PathValue> ShortestPathPhysicalOp::allShortestPaths(VertexId start, VertexId end) {
    auto one = co_await shortestBidirectional(start, end);
    if (!one.has_value())
        co_return;
    const size_t best = (one->elements.size() - 1) / 2;
    if (best == 0) {
        co_yield std::move(*one);
        co_return;
    }

    std::unordered_map<VertexId, AllPathNode> visits;
    std::vector<VertexId> frontier{start};
    visits.emplace(start, AllPathNode{0, {}});

    for (size_t depth = 1; depth <= best && !frontier.empty(); ++depth) {
        std::vector<VertexId> next;
        auto frontier_lists = co_await scanBatch(frontier, forward_dir_);
        for (size_t fi = 0; fi < frontier.size(); ++fi) {
            if (cancelled())
                co_return;
            const VertexId v = frontier[fi];
            const auto& edges = *frontier_lists[fi];
            for (const auto& e : edges) {
                if (e.id == v || e.id == start)
                    continue;
                // The endpoint may be reachable below the lower hop bound.
                // It must not become a visited terminal in that case; keep
                // searching until the first valid depth (best) and only then
                // record parents. Otherwise a direct edge would incorrectly
                // satisfy a `*2..3` allShortestPaths query.
                if (e.id == end && depth < best)
                    continue;
                auto [it, inserted] = visits.try_emplace(e.id, AllPathNode{static_cast<int>(depth), {}});
                if (inserted) {
                    it->second.parents.push_back(e.link);
                    next.push_back(e.id);
                } else if (it->second.dist == static_cast<int>(depth)) {
                    it->second.parents.push_back(e.link);
                }
            }
        }
        frontier = std::move(next);
    }

    struct Frame {
        VertexId v;
        size_t next_parent = 0;
    };
    std::vector<Frame> stack{{end, 0}};
    std::vector<EdgeLink> reversed;
    size_t produced = 0;
    while (!stack.empty()) {
        Frame& frame = stack.back();
        if (frame.v == start) {
            std::vector<EdgeLink> links(reversed.rbegin(), reversed.rend());
            if (produced++ >= MAX_PATHS)
                throw std::runtime_error("MemoryLimitExceeded: too many shortest paths");
            co_yield buildPath(start, links);
            stack.pop_back();
            if (!stack.empty() && !reversed.empty())
                reversed.pop_back();
            continue;
        }
        auto it = visits.find(frame.v);
        if (it == visits.end() || frame.next_parent >= it->second.parents.size()) {
            stack.pop_back();
            if (!stack.empty() && !reversed.empty())
                reversed.pop_back();
            continue;
        }
        const EdgeLink link = it->second.parents[frame.next_parent++];
        reversed.push_back(link);
        stack.push_back(Frame{link.from, 0});
    }
}

folly::coro::AsyncGenerator<DataChunk> ShortestPathPhysicalOp::executeChunk() {
    if (dst_label_missing_)
        co_return;

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

    auto child_gen = cancellable(child_->executeChunkTraced());
    while (auto chunk = co_await child_gen.next()) {
        if (!chunk || chunk->numRows() == 0)
            continue;
        const size_t input_rows = chunk->numRows();
        const size_t input_cols = chunk->numColumns();
        std::vector<std::pair<size_t, PathValue>> pending;
        size_t pending_path_elements = 0;

        auto flush = [&]() -> DataChunk {
            const size_t n = pending.size();
            DataChunk output;
            output.columns.reserve(input_cols + (path_var_.empty() ? 0 : 1));
            for (size_t c = 0; c < input_cols; ++c) {
                output.columns.push_back(Column::flat(chunk->columns[c].type, n));
                for (size_t r = 0; r < n; ++r)
                    output.columns[c].setValue(r, chunk->columns[c].getValue(pending[r].first));
            }
            if (!path_var_.empty()) {
                output.columns.push_back(Column::flat(binder::BoundTypeKind::PATH, n));
                for (size_t r = 0; r < n; ++r)
                    output.columns[input_cols].setValue(r, Value(mk<PathValue>(std::move(pending[r].second))));
            }
            pending.clear();
            pending_path_elements = 0;
            output.count = n;
            output.sel = SelectionVector::identity(n);
            return output;
        };

        for (size_t row = 0; row < input_rows; ++row) {
            if (src_col_idx_ < 0 || dst_col_idx_ < 0)
                continue;
            VertexId start = vertexIdFromValue(chunk->columns[src_col_idx_].getValue(row));
            VertexId end = vertexIdFromValue(chunk->columns[dst_col_idx_].getValue(row));
            if (start == INVALID_VERTEX_ID || end == INVALID_VERTEX_ID)
                continue;
            if (src_filter_index_id_ != 0 && !src_allowed.count(start))
                continue;
            if (dst_filter_index_id_ != 0 && !dst_allowed.count(end))
                continue;
            if (!co_await hasDstLabels(end))
                continue;

            if (all_shortest_paths_) {
                auto paths = allShortestPaths(start, end);
                while (auto path = co_await paths.next()) {
                    pending_path_elements += path->elements.size();
                    pending.emplace_back(row, std::move(*path));
                    if (pending.size() >= DataChunk::DEFAULT_CAPACITY ||
                        pending_path_elements >= MAX_PENDING_PATH_ELEMENTS)
                        co_yield flush();
                }
            } else {
                auto path = co_await shortestBidirectional(start, end);
                if (path.has_value()) {
                    pending_path_elements += path->elements.size();
                    pending.emplace_back(row, std::move(*path));
                }
            }
            if (pending.size() >= DataChunk::DEFAULT_CAPACITY || pending_path_elements >= MAX_PENDING_PATH_ELEMENTS)
                co_yield flush();
        }
        if (!pending.empty())
            co_yield flush();
    }
}

} // namespace compute
} // namespace eugraph
