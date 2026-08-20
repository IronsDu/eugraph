#include "query/physical_plan/operator/delete_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "query/dataset/row.hpp"
#include "query/evaluator/vectorized_evaluator.hpp"
#include "query/physical_plan/expression_compiler.hpp"

#include <stdexcept>
#include <unordered_set>
#include <vector>

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

/// One entity to delete, either an edge or a vertex.
struct DeleteEntity {
    bool is_edge = false;
    EdgeId edge_id = INVALID_EDGE_ID;
    EdgeLabelId edge_label_id = INVALID_EDGE_LABEL_ID;
    VertexId edge_src = INVALID_VERTEX_ID;
    VertexId edge_dst = INVALID_VERTEX_ID;
    uint64_t seq = 0;
    VertexId vertex_id = INVALID_VERTEX_ID;
};

void collectDeleteEntities(const Value& value, std::vector<DeleteEntity>& out) {
    if (isNull(value)) {
        return;
    } else if (std::holds_alternative<VertexValue>(value)) {
        const auto& v = std::get<VertexValue>(value);
        out.push_back({false, INVALID_EDGE_ID, INVALID_EDGE_LABEL_ID, INVALID_VERTEX_ID, INVALID_VERTEX_ID, 0, v.id});
    } else if (std::holds_alternative<VertexRef>(value)) {
        const auto& v = std::get<VertexRef>(value);
        out.push_back({false, INVALID_EDGE_ID, INVALID_EDGE_LABEL_ID, INVALID_VERTEX_ID, INVALID_VERTEX_ID, 0, v.id});
    } else if (std::holds_alternative<EdgeValue>(value)) {
        const auto& e = std::get<EdgeValue>(value);
        out.push_back({true, e.id, e.label_id, e.src_id, e.dst_id, e.seq, INVALID_VERTEX_ID});
    } else if (std::holds_alternative<EdgeKey>(value)) {
        const auto& e = std::get<EdgeKey>(value);
        out.push_back({true, e.id, e.label_id, e.src_id, e.dst_id, e.seq, INVALID_VERTEX_ID});
    } else if (std::holds_alternative<PathValue>(value)) {
        for (const auto& elem : std::get<PathValue>(value).elements)
            collectDeleteEntities(elem.value, out);
    } else if (std::holds_alternative<PathTopology>(value)) {
        const auto& p = std::get<PathTopology>(value);
        for (size_t i = 0; i < p.edge_ids.size(); ++i) {
            out.push_back({true, p.edge_ids[i], p.edge_label_ids[i], p.edge_src_ids[i], p.edge_dst_ids[i], p.seqs[i],
                           INVALID_VERTEX_ID});
        }
        for (VertexId vid : p.vertex_ids)
            out.push_back(
                {false, INVALID_EDGE_ID, INVALID_EDGE_LABEL_ID, INVALID_VERTEX_ID, INVALID_VERTEX_ID, 0, vid});
    } else if (std::holds_alternative<ListValue>(value)) {
        for (const auto& elem : std::get<ListValue>(value).elements)
            collectDeleteEntities(elem.value, out);
    } else if (std::holds_alternative<MapValue>(value)) {
        for (const auto& [k, v] : std::get<MapValue>(value).entries) {
            (void)k;
            collectDeleteEntities(v.value, out);
        }
    } else {
        throw std::runtime_error("TypeError: InvalidArgumentType: DELETE expression must evaluate to a node, "
                                 "relationship, path, list, or map");
    }
}

Value evaluateDeleteExpr(VectorizedEvaluator& evaluator, const binder::BoundExpression& expr, const DataChunk& chunk,
                         size_t row_idx) {
    DataChunk single;
    single.count = 1;
    for (size_t c = 0; c < chunk.numColumns(); ++c) {
        Column col = Column::flat(chunk.columns[c].type, 1);
        col.setValue(0, chunk.getValue(c, row_idx));
        single.columns.push_back(std::move(col));
    }
    Column result = Column::flat(binder::BoundTypeKind::ANY, 1);
    evaluator.evaluate(expr, single, result);
    return result.getValue(0);
}

} // anonymous namespace

void DeletePhysicalOp::compileExpressions(const TupleSlotLayout& input_layout) {
    ExpressionCompiler compiler(input_layout);
    for (auto& target : targets_) {
        if (target.expr)
            compiler.compile(*target.expr);
    }
}

folly::coro::AsyncGenerator<DataChunk> DeletePhysicalOp::executeChunk() {
    auto child_gen = child_->executeChunk();
    VectorizedEvaluator evaluator(eval_ctx_);

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
            // Collect every entity referenced by this row's DELETE targets.
            std::vector<DeleteEntity> entities;
            for (const auto& target : targets_) {
                if (target.expr) {
                    Value val = evaluateDeleteExpr(evaluator, *target.expr, *chunk, row_idx);
                    collectDeleteEntities(val, entities);
                } else if (target.kind) {
                    int col = target.object_col >= 0 ? target.object_col : findColumn(input_schema_, target.var_name);
                    if (col < 0 || static_cast<size_t>(col) >= chunk->numColumns())
                        continue;
                    Value val = chunk->getValue(static_cast<size_t>(col), row_idx);
                    collectDeleteEntities(val, entities);
                }
            }

            // Pass 1: delete edges first so connected-vertex checks in pass 2
            // can skip edges deleted in the same statement.
            for (const auto& entity : entities) {
                if (!entity.is_edge)
                    continue;
                if (deleted_edges.insert(entity.edge_id).second)
                    co_await store_.deleteEdge(entity.edge_id, entity.edge_label_id, entity.edge_src, entity.edge_dst,
                                               entity.seq);
            }

            // Pass 2: delete vertices.
            for (const auto& entity : entities) {
                if (entity.is_edge)
                    continue;
                VertexId vid = entity.vertex_id;
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
            }

            // Write back deleted flags for simple variable targets so
            // downstream expressions (e.g. type(r) after DELETE r) observe
            // the deletion.
            for (const auto& target : targets_) {
                if (target.expr || !target.kind)
                    continue;
                int col = target.object_col >= 0 ? target.object_col : findColumn(input_schema_, target.var_name);
                if (col < 0 || static_cast<size_t>(col) >= chunk->numColumns())
                    continue;
                Value val = chunk->getValue(static_cast<size_t>(col), row_idx);
                if (*target.kind == TargetKind::EDGE && std::holds_alternative<EdgeValue>(val)) {
                    auto edge = std::get<EdgeValue>(val);
                    edge.deleted = true;
                    writeBack(col, row_idx, Value(std::move(edge)));
                } else if (*target.kind == TargetKind::VERTEX && std::holds_alternative<VertexValue>(val)) {
                    auto vertex = std::get<VertexValue>(val);
                    vertex.deleted = true;
                    writeBack(col, row_idx, Value(std::move(vertex)));
                }
            }
        }
        co_yield std::move(*chunk);
    }
}

} // namespace compute
} // namespace eugraph
