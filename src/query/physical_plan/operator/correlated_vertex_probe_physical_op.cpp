#include "query/physical_plan/operator/correlated_vertex_probe_physical_op.hpp"

namespace eugraph {
namespace compute {

namespace {

VertexId vertexIdOf(const Value& v) {
    if (std::holds_alternative<VertexRef>(v))
        return std::get<VertexRef>(v).id;
    if (std::holds_alternative<VertexValuePtr>(v))
        return (*std::get<VertexValuePtr>(v)).id;
    if (std::holds_alternative<int64_t>(v))
        return static_cast<VertexId>(std::get<int64_t>(v));
    return INVALID_VERTEX_ID;
}

bool edgeEnds(const Value& v, VertexId& src, VertexId& dst) {
    if (std::holds_alternative<EdgeKey>(v)) {
        const auto& e = std::get<EdgeKey>(v);
        src = e.src_id;
        dst = e.dst_id;
        return true;
    }
    if (std::holds_alternative<EdgeValuePtr>(v)) {
        const auto& e = *std::get<EdgeValuePtr>(v);
        src = e.src_id;
        dst = e.dst_id;
        return true;
    }
    return false;
}

DataChunk makeChunk(const std::vector<binder::BoundType>& types) {
    DataChunk out;
    out.columns.reserve(types.size());
    for (const auto& t : types)
        out.columns.push_back(Column::flat(t.kind, DataChunk::DEFAULT_CAPACITY));
    return out;
}

void reserveChunk(DataChunk& out) {
    for (auto& c : out.columns)
        c.reserve(DataChunk::DEFAULT_CAPACITY);
}

} // namespace

folly::coro::AsyncGenerator<DataChunk> CorrelatedVertexProbePhysicalOp::executeChunk() {
    if (!cache_built_ && !cache_disabled_) {
        cache_built_ = true;
        size_t total = 0;
        auto right_gen = cancellable(right_->executeChunkTraced());
        while (auto chunk = co_await right_gen.next()) {
            if (chunk->count == 0)
                continue;
            if (total + chunk->count > kMaxCachedRows) {
                by_vertex_.clear();
                cache_disabled_ = true;
                break;
            }
            total += chunk->count;
            for (size_t r = 0; r < chunk->count; ++r) {
                VertexId vid = vertexIdOf(chunk->getValue(right_vertex_col_, r));
                if (vid == INVALID_VERTEX_ID)
                    continue;
                Row row;
                row.reserve(chunk->columns.size());
                for (size_t c = 0; c < chunk->columns.size(); ++c)
                    row.push_back(chunk->getValue(c, r));
                by_vertex_[vid].push_back(std::move(row));
            }
        }
    }

    auto left_gen = cancellable(left_->executeChunkTraced());
    while (auto left_chunk = co_await left_gen.next()) {
        if (left_chunk->count == 0)
            continue;

        DataChunk out = makeChunk(output_types_);
        reserveChunk(out);

        for (size_t r = 0; r < left_chunk->count; ++r) {
            VertexId src = INVALID_VERTEX_ID;
            VertexId dst = INVALID_VERTEX_ID;
            if (!edgeEnds(left_chunk->getValue(left_edge_col_, r), src, dst))
                continue;

            if (cache_disabled_) {
                auto right_gen = cancellable(right_->executeChunkTraced());
                while (auto right_chunk = co_await right_gen.next()) {
                    for (size_t rr = 0; rr < right_chunk->count; ++rr) {
                        VertexId vid = vertexIdOf(right_chunk->getValue(right_vertex_col_, rr));
                        if (vid != src && vid != dst)
                            continue;
                        if (out.count >= DataChunk::DEFAULT_CAPACITY) {
                            out.sel = SelectionVector::identity(out.count);
                            co_yield std::move(out);
                            out = makeChunk(output_types_);
                            reserveChunk(out);
                        }
                        for (size_t lc = 0; lc < left_schema_.size(); ++lc)
                            out.columns[lc].setValue(out.count, left_chunk->getValue(lc, r));
                        for (size_t rc = 0; rc < right_schema_.size(); ++rc)
                            out.columns[left_schema_.size() + rc].setValue(out.count, right_chunk->getValue(rc, rr));
                        ++out.count;
                    }
                }
                continue;
            }

            std::vector<const std::vector<Row>*> matches;
            auto add = [&](VertexId vid) {
                if (vid == INVALID_VERTEX_ID)
                    return;
                auto it = by_vertex_.find(vid);
                if (it != by_vertex_.end())
                    matches.push_back(&it->second);
            };
            add(src);
            if (dst != src)
                add(dst);

            for (const auto* list : matches) {
                for (const auto& rr : *list) {
                    if (out.count >= DataChunk::DEFAULT_CAPACITY) {
                        out.sel = SelectionVector::identity(out.count);
                        co_yield std::move(out);
                        out = makeChunk(output_types_);
                        reserveChunk(out);
                    }
                    for (size_t lc = 0; lc < left_schema_.size(); ++lc)
                        out.columns[lc].setValue(out.count, left_chunk->getValue(lc, r));
                    for (size_t rc = 0; rc < right_schema_.size(); ++rc)
                        out.columns[left_schema_.size() + rc].setValue(out.count, rr[rc]);
                    ++out.count;
                }
            }
        }

        if (out.count > 0) {
            out.sel = SelectionVector::identity(out.count);
            co_yield std::move(out);
        }
    }
}

} // namespace compute
} // namespace eugraph
