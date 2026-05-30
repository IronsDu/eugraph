#include "query/physical_plan/operator/create_edge_physical_op.hpp"
#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"
#include "common/types/temporal_value.hpp"
#include "query/dataset/row.hpp"
#include "query/evaluator/vectorized_evaluator.hpp"
#include "storage/kv/value_codec.hpp"

#include <spdlog/spdlog.h>

namespace {
eugraph::PropertyValue valueToPropertyValue(const eugraph::Value& v) {
    if (std::holds_alternative<bool>(v))
        return std::get<bool>(v);
    if (std::holds_alternative<int64_t>(v))
        return std::get<int64_t>(v);
    if (std::holds_alternative<double>(v))
        return std::get<double>(v);
    if (std::holds_alternative<std::string>(v))
        return std::get<std::string>(v);
    if (std::holds_alternative<eugraph::DateTimeValue>(v))
        return std::get<eugraph::DateTimeValue>(v);
    if (std::holds_alternative<eugraph::TimeValue>(v))
        return std::get<eugraph::TimeValue>(v);
    if (std::holds_alternative<eugraph::DurationValue>(v))
        return std::get<eugraph::DurationValue>(v);
    return eugraph::PropertyValue{};
}

eugraph::Value evaluateExpr(eugraph::compute::VectorizedEvaluator& evaluator,
                            const eugraph::binder::BoundExpression& expr, const eugraph::DataChunk* chunk,
                            size_t row_idx) {
    if (chunk && chunk->count > 0) {
        eugraph::DataChunk single_row;
        single_row.count = 1;
        for (size_t c = 0; c < chunk->numColumns(); ++c) {
            eugraph::Column col = eugraph::Column::flat(chunk->columns[c].type, 1);
            col.setValue(0, chunk->getValue(c, row_idx));
            single_row.columns.push_back(std::move(col));
        }
        eugraph::Column result_col = eugraph::Column::flat(eugraph::binder::BoundTypeKind::ANY, 1);
        evaluator.evaluate(expr, single_row, result_col);
        return result_col.getValue(0);
    }
    eugraph::DataChunk empty;
    empty.count = 1;
    eugraph::Column result_col = eugraph::Column::flat(eugraph::binder::BoundTypeKind::ANY, 1);
    evaluator.evaluate(expr, empty, result_col);
    return result_col.getValue(0);
}

eugraph::VertexId extractVidFromColumn(const eugraph::Column& col, size_t row_idx) {
    const auto& val = col.getValue(row_idx);
    if (std::holds_alternative<eugraph::VertexValue>(val))
        return std::get<eugraph::VertexValue>(val).id;
    return 0;
}

} // namespace

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> CreateEdgePhysicalOp::executeChunk() {
    VectorizedEvaluator evaluator(eval_ctx_);

    // Resolve effective label_id
    EdgeLabelId effective_label_id = INVALID_EDGE_LABEL_ID;
    if (label_id_.has_value()) {
        effective_label_id = *label_id_;
    } else if (label_name_.has_value()) {
        auto it = edge_label_name_to_id_.find(*label_name_);
        if (it != edge_label_name_to_id_.end()) {
            effective_label_id = it->second;
        }
    }

    const EdgeLabelDef* edge_label_def = nullptr;
    if (effective_label_id != INVALID_EDGE_LABEL_ID) {
        auto def_it = edge_label_defs_.find(effective_label_id);
        if (def_it != edge_label_defs_.end()) {
            edge_label_def = &def_it->second;
        }
    }

    // Resolve pending_props_ by name → (pid, expr) after DDL operators ran
    std::vector<std::pair<uint16_t, binder::BoundExpression>> resolved_pending;
    for (auto& [prop_name, expr] : pending_props_) {
        if (!edge_label_def)
            continue;
        for (const auto& pd : edge_label_def->properties) {
            if (pd.name == prop_name) {
                resolved_pending.emplace_back(pd.id, std::move(expr));
                break;
            }
        }
    }

    // Lambda: build Properties for a single row
    auto buildProps = [&](const DataChunk* chunk, size_t row_idx) -> Properties {
        Properties props;
        for (const auto& [pid, expr] : prop_exprs_) {
            Value v = evaluateExpr(evaluator, expr, chunk, row_idx);
            if (!std::holds_alternative<std::monostate>(v)) {
                if (props.size() <= pid)
                    props.resize(pid + 1);
                props[pid] = valueToPropertyValue(v);
            }
        }
        for (const auto& [pid, expr] : resolved_pending) {
            Value v = evaluateExpr(evaluator, expr, chunk, row_idx);
            if (!std::holds_alternative<std::monostate>(v)) {
                if (props.size() <= pid)
                    props.resize(pid + 1);
                props[pid] = valueToPropertyValue(v);
            }
        }
        return props;
    };

    // Lambda: check constraints and insert one edge
    auto insertEdge = [&](EdgeId eid, VertexId src, VertexId dst, const Properties& props) -> folly::coro::Task<bool> {
        bool ok = true;
        if (edge_label_def) {
            for (const auto& idx : edge_label_def->indexes) {
                if (!idx.unique)
                    continue;
                if (idx.state != IndexState::WRITE_ONLY && idx.state != IndexState::PUBLIC)
                    continue;

                std::vector<PropertyValue> values;
                bool allPresent = true;
                for (auto prop_id : idx.prop_ids) {
                    if (prop_id < props.size() && props[prop_id].has_value()) {
                        values.push_back(props[prop_id].value());
                    } else {
                        allPresent = false;
                        break;
                    }
                }
                if (!allPresent)
                    continue;

                auto table = idx.prop_ids.size() == 1 ? eidxTable(effective_label_id, idx.prop_ids[0])
                                                      : eidxCompositeTable(effective_label_id, idx.prop_ids);
                bool constraint_ok = co_await store_.checkUniqueConstraint(table, values);
                if (!constraint_ok) {
                    spdlog::warn("Unique edge index constraint violated on index '{}'", idx.name);
                    ok = false;
                    break;
                }
            }
        }

        if (ok)
            ok = co_await store_.insertEdge(eid, src, dst, effective_label_id, 0, props);

        if (ok && edge_label_def) {
            for (const auto& idx : edge_label_def->indexes) {
                if (idx.state != IndexState::WRITE_ONLY && idx.state != IndexState::PUBLIC)
                    continue;

                std::vector<PropertyValue> values;
                bool allPresent = true;
                for (auto prop_id : idx.prop_ids) {
                    if (prop_id < props.size() && props[prop_id].has_value()) {
                        values.push_back(props[prop_id].value());
                    } else {
                        allPresent = false;
                        break;
                    }
                }
                if (!allPresent)
                    continue;

                auto adj_value = ValueCodec::encodeEdgeAdjacency(src, dst, 0, effective_label_id);
                auto table = idx.prop_ids.size() == 1 ? eidxTable(effective_label_id, idx.prop_ids[0])
                                                      : eidxCompositeTable(effective_label_id, idx.prop_ids);
                co_await store_.insertIndexEntry(table, values, eid, std::move(adj_value));
            }
        }
        co_return ok;
    };

    // Per-row edge creation
    if (child_) {
        auto child_gen = child_->executeChunk();
        while (auto chunk = co_await child_gen.next()) {
            for (size_t row = 0; row < chunk->count; ++row) {
                VertexId src = extractVidFromColumn(chunk->columns[src_col_idx_], row);
                VertexId dst = extractVidFromColumn(chunk->columns[dst_col_idx_], row);

                EdgeId eid = co_await meta_.nextEdgeId();
                auto props = buildProps(&*chunk, row);

                bool ok = co_await insertEdge(eid, src, dst, props);
                if (ok) {
                    // Preserve child columns + append new edge column
                    DataChunk output;
                    for (size_t c = 0; c < chunk->numColumns(); ++c) {
                        Column col = Column::flat(chunk->columns[c].type, 1);
                        col.setValue(0, chunk->getValue(c, row));
                        output.columns.push_back(std::move(col));
                    }

                    EdgeValue ev;
                    ev.id = eid;
                    ev.src_id = src;
                    ev.dst_id = dst;
                    ev.label_id = effective_label_id;
                    ev.seq = 0;

                    Column edge_col = Column::flat(binder::BoundTypeKind::EDGE, 1);
                    edge_col.setValue(0, Value(std::move(ev)));
                    output.columns.push_back(std::move(edge_col));
                    output.count = 1;
                    co_yield std::move(output);
                }
            }
        }
    }
}

} // namespace compute
} // namespace eugraph
