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

folly::coro::Task<std::shared_ptr<const ShortestPathPhysicalOp::NeighborList>>
ShortestPathPhysicalOp::scan(VertexId vid, Direction scan_dir) {
    const uint64_t key = (static_cast<uint64_t>(vid) << 2) | static_cast<uint64_t>(scan_dir);
    if (auto it = edge_cache_.find(key); it != edge_cache_.end())
        co_return it->second;

    auto out = std::make_shared<NeighborList>();
    auto append = [&](Direction dir) -> folly::coro::Task<void> {
        for (const auto& label_filter : scan_filters_) {
            auto gen = cancellable(store_.scanEdges(vid, dir, label_filter));
            while (auto batch = co_await gen.next()) {
                for (const auto& e : *batch) {
                    EdgeLink link;
                    link.from = vid;
                    link.to = e.neighbor_id;
                    if (dir == Direction::OUT) {
                        link.src = vid;
                        link.dst = e.neighbor_id;
                    } else {
                        link.src = e.neighbor_id;
                        link.dst = vid;
                    }
                    link.edge_id = e.edge_id;
                    link.edge_label_id = e.edge_label_id;
                    link.seq = e.seq;
                    out->push_back(Neighbor{e.neighbor_id, link});
                }
            }
        }
        co_return;
    };

    if (scan_dir == Direction::BOTH) {
        co_await append(Direction::OUT);
        co_await append(Direction::IN);
    } else {
        co_await append(scan_dir);
    }
    if (cached_neighbors_ + out->size() <= MAX_CACHED_NEIGHBORS) {
        cached_neighbors_ += out->size();
        edge_cache_.emplace(key, out);
    }
    co_return out;
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
            for (VertexId v : f_frontier) {
                if (cancelled())
                    co_return std::nullopt;
                auto edges = co_await scan(v, forward_dir_);
                for (const auto& e : *edges) {
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
            for (VertexId v : b_frontier) {
                if (cancelled())
                    co_return std::nullopt;
                auto edges = co_await scan(v, backward_dir_);
                for (const auto& e : *edges) {
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
        for (VertexId v : frontier) {
            if (cancelled())
                co_return;
            auto edges = co_await scan(v, forward_dir_);
            for (const auto& e : *edges) {
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
        auto gen = cancellable(store_.scanVerticesByIndexId(index_id, one));
        while (auto batch = co_await gen.next()) {
            for (VertexId vid : *batch)
                out.insert(vid);
        }
    };
    if (src_filter_index_id_ != 0)
        co_await loadAllowed(src_filter_index_id_, src_filter_value_, src_allowed);
    if (dst_filter_index_id_ != 0)
        co_await loadAllowed(dst_filter_index_id_, dst_filter_value_, dst_allowed);

    auto child_gen = cancellable(child_->executeChunk());
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
