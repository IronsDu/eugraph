#include "query/physical_plan/operator/delete_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "query/dataset/row.hpp"

#include <spdlog/spdlog.h>
#include <unordered_set>

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
        std::unordered_set<VertexId> checked_vertices;
        std::unordered_set<EdgeId> deleted_edges;

        // Helper to write back a modified value through DICTIONARY columns.
        auto writeBack = [&](size_t col, size_t row_idx, const Value& val) {
            Column& dcol = chunk->columns[static_cast<size_t>(col)];
            if (dcol.form == VectorForm::DICTIONARY && dcol.buffer) {
                size_t physical = dcol.dict_sel[row_idx];
                dcol.buffer->setValue(physical, val);
            } else {
                dcol.setValue(row_idx, val);
            }
        };

        for (size_t row_idx = 0; row_idx < n; ++row_idx) {
            // Pass 1: delete edges first so that connected-vertex checks
            // in pass 2 can skip edges deleted in the same statement
            // (e.g. DELETE a, r, b where r connects a and b).
            for (const auto& target : targets_) {
                if (target.kind != TargetKind::EDGE)
                    continue;
                int col = findColumn(input_schema_, target.var_name);
                if (col < 0 || static_cast<size_t>(col) >= chunk->numColumns())
                    continue;
                Value val = chunk->getValue(static_cast<size_t>(col), row_idx);
                if (!std::holds_alternative<EdgeValue>(val))
                    continue;
                auto& edge = std::get<EdgeValue>(val);
                deleted_edges.insert(edge.id);
                co_await store_.deleteEdge(edge.id, edge.label_id, edge.src_id, edge.dst_id, edge.seq);
                edge.deleted = true;
                writeBack(col, row_idx, val);
            }

            // Pass 2: delete vertices.
            for (const auto& target : targets_) {
                if (target.kind != TargetKind::VERTEX)
                    continue;
                int col = findColumn(input_schema_, target.var_name);
                if (col < 0 || static_cast<size_t>(col) >= chunk->numColumns())
                    continue;
                Value val = chunk->getValue(static_cast<size_t>(col), row_idx);
                if (!std::holds_alternative<VertexValue>(val))
                    continue;
                auto& vertex = std::get<VertexValue>(val);
                VertexId vid = vertex.id;

                if (detach_) {
                    auto out_gen = store_.scanEdges(vid, Direction::OUT, std::nullopt);
                    while (auto edge_batch = co_await out_gen.next()) {
                        for (const auto& entry : *edge_batch)
                            co_await store_.deleteEdge(entry.edge_id, entry.edge_label_id, vid, entry.neighbor_id,
                                                       entry.seq);
                    }
                    auto in_gen = store_.scanEdges(vid, Direction::IN, std::nullopt);
                    while (auto edge_batch = co_await in_gen.next()) {
                        for (const auto& entry : *edge_batch)
                            co_await store_.deleteEdge(entry.edge_id, entry.edge_label_id, entry.neighbor_id, vid,
                                                       entry.seq);
                    }
                } else if (checked_vertices.insert(vid).second) {
                    bool has_connected = false;
                    auto out_gen = store_.scanEdges(vid, Direction::OUT, std::nullopt);
                    while (auto batch = co_await out_gen.next()) {
                        for (const auto& entry : *batch) {
                            if (deleted_edges.find(entry.edge_id) == deleted_edges.end()) {
                                has_connected = true;
                                break;
                            }
                        }
                        if (has_connected)
                            break;
                    }
                    if (!has_connected) {
                        auto in_gen = store_.scanEdges(vid, Direction::IN, std::nullopt);
                        while (auto batch = co_await in_gen.next()) {
                            for (const auto& entry : *batch) {
                                if (deleted_edges.find(entry.edge_id) == deleted_edges.end()) {
                                    has_connected = true;
                                    break;
                                }
                            }
                            if (has_connected)
                                break;
                        }
                    }
                    if (has_connected)
                        throw std::runtime_error("ConstraintVerificationFailed: DeleteConnectedNode: "
                                                 "node has connected edges");
                }
                co_await store_.deleteVertex(vid);
                vertex.deleted = true;
                writeBack(col, row_idx, val);
            }
        }
        co_yield std::move(*chunk);
    }
}

} // namespace compute
} // namespace eugraph
