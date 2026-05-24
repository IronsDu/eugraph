#include "query/physical_plan/operator/delete_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "query/dataset/row.hpp"

namespace eugraph {
namespace compute {

namespace {

int findColumn(const Schema& schema, const std::string& name) {
    for (size_t i = 0; i < schema.size(); ++i) {
        if (schema[i] == name)
            return static_cast<int>(i);
    }
    return -1;
}

} // anonymous namespace

folly::coro::AsyncGenerator<DataChunk> DeletePhysicalOp::executeChunk() {
    auto child_gen = child_->executeChunk();

    while (auto chunk = co_await child_gen.next()) {
        size_t n = chunk->numRows();
        for (size_t row_idx = 0; row_idx < n; ++row_idx) {
            for (const auto& target : targets_) {
                int col = findColumn(input_schema_, target.var_name);
                if (col < 0 || static_cast<size_t>(col) >= chunk->numColumns())
                    continue;

                Value val = chunk->getValue(static_cast<size_t>(col), row_idx);

                if (target.kind == TargetKind::VERTEX) {
                    if (!std::holds_alternative<VertexValue>(val))
                        continue;
                    const auto& vertex = std::get<VertexValue>(val);
                    VertexId vid = vertex.id;

                    if (detach_) {
                        // Delete outgoing edges: vid is src, neighbor is dst
                        auto out_gen = store_.scanEdges(vid, Direction::OUT, std::nullopt);
                        while (auto edge_batch = co_await out_gen.next()) {
                            for (const auto& entry : *edge_batch) {
                                co_await store_.deleteEdge(entry.edge_id, entry.edge_label_id, vid, entry.neighbor_id,
                                                           entry.seq);
                            }
                        }
                        // Delete incoming edges: neighbor is src, vid is dst
                        auto in_gen = store_.scanEdges(vid, Direction::IN, std::nullopt);
                        while (auto edge_batch = co_await in_gen.next()) {
                            for (const auto& entry : *edge_batch) {
                                co_await store_.deleteEdge(entry.edge_id, entry.edge_label_id, entry.neighbor_id, vid,
                                                           entry.seq);
                            }
                        }
                    }
                    co_await store_.deleteVertex(vid);
                } else {
                    if (!std::holds_alternative<EdgeValue>(val))
                        continue;
                    const auto& edge = std::get<EdgeValue>(val);
                    co_await store_.deleteEdge(edge.id, edge.label_id, edge.src_id, edge.dst_id, edge.seq);
                }
            }
        }
        co_yield std::move(*chunk);
    }
}

} // namespace compute
} // namespace eugraph
