#include "query/physical_plan/operator/create_node_physical_op.hpp"
#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"
#include "query/dataset/row.hpp"
#include "query/evaluator/vectorized_evaluator.hpp"
#include "query/physical_plan/operator/property_value_convert.hpp"
#include <spdlog/spdlog.h>

namespace {
eugraph::LabelId getAnonLabelId(const std::unordered_map<eugraph::LabelId, eugraph::LabelDef>& label_defs) {
    for (const auto& [lid, def] : label_defs) {
        if (def.name == eugraph::kAnonLabelName)
            return lid;
    }
    return eugraph::INVALID_LABEL_ID;
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

} // namespace

namespace eugraph {
namespace compute {

std::string CreateNodePhysicalOp::toString() const {
    std::string s;
    for (size_t i = 0; i < label_ids_.size(); i++) {
        if (i > 0)
            s += ", ";
        if (!label_defs_.empty()) {
            auto it = label_defs_.find(label_ids_[i]);
            s += (it != label_defs_.end()) ? it->second.name : std::to_string(label_ids_[i]);
        } else {
            s += std::to_string(label_ids_[i]);
        }
    }
    return "CreateNode(variable=" + variable_ + ", labels=[" + s + "])";
}

folly::coro::AsyncGenerator<DataChunk> CreateNodePhysicalOp::executeChunk() {
    VectorizedEvaluator evaluator(eval_ctx_);

    // Phase 1: ensure all label data tables exist
    for (auto lid : label_ids_) {
        if (lid == INVALID_LABEL_ID)
            continue;
        auto it = label_defs_.find(lid);
        if (it == label_defs_.end()) {
            auto def = co_await meta_.getLabelDefById(lid);
            if (def) {
                label_defs_[lid] = std::move(*def);
            }
        }
        // Ensure data table exists
        co_await store_.createLabel(lid);
    }

    // Phase 1b: __anon__ attribute lightweight registration (only once).
    // Triggers on either pending props (label-free property bag) or a fully
    // bare CREATE () — the latter still needs __anon__ so the vertex has at
    // least one label-reverse key and is discoverable by AllNodeScan.
    if (!anon_registered_ && (!pending_props_.empty() || label_ids_.empty())) {
        LabelId anon_lid = getAnonLabelId(label_defs_);
        if (anon_lid == INVALID_LABEL_ID) {
            // Auto-create __anon__ label if it doesn't exist
            anon_lid = co_await meta_.createLabel(std::string(kAnonLabelName), {});
            if (anon_lid != INVALID_LABEL_ID) {
                co_await store_.createLabel(anon_lid);
                auto def = co_await meta_.getLabelDefById(anon_lid);
                if (def)
                    label_defs_[anon_lid] = std::move(*def);
            }
        }
        if (anon_lid != INVALID_LABEL_ID) {
            for (auto& [prop_name, expr] : pending_props_) {
                uint16_t pid = co_await meta_.getOrCreateAnonPropId(prop_name, PropertyType::ANY);
                resolved_pending_.emplace_back(anon_lid, pid, std::move(expr));
            }
            auto updated_def = co_await meta_.getLabelDefById(anon_lid);
            if (updated_def) {
                label_defs_[anon_lid] = std::move(*updated_def);
            }
            // Bare CREATE () with no labels and no props: still assign __anon__
            // so the vertex is persisted with a label-reverse key and becomes
            // discoverable by AllNodeScan / getVertexLabels.
            if (label_ids_.empty()) {
                label_ids_.push_back(anon_lid);
                co_await store_.createLabel(anon_lid);
            }
        }
        anon_registered_ = true;
    }

    // Lambda: build per-label Properties vectors
    auto buildLabelProps = [&](const DataChunk* chunk, size_t row_idx) -> std::vector<std::pair<LabelId, Properties>> {
        std::vector<std::pair<LabelId, Properties>> result;

        for (auto lid : label_ids_) {
            if (lid == INVALID_LABEL_ID)
                continue;
            Properties props;

            for (const auto& [expr_lid, exprs] : label_prop_exprs_) {
                if (expr_lid != lid)
                    continue;
                for (const auto& [pid, expr] : exprs) {
                    Value v = evaluateExpr(evaluator, expr, chunk, row_idx);
                    if (!std::holds_alternative<std::monostate>(v)) {
                        if (props.size() <= pid)
                            props.resize(pid + 1);
                        props[pid] = valueToPropertyValue(v);
                    }
                }
            }

            for (const auto& [plid, pid, expr] : resolved_pending_) {
                if (plid != lid)
                    continue;
                Value v = evaluateExpr(evaluator, expr, chunk, row_idx);
                if (!std::holds_alternative<std::monostate>(v)) {
                    if (props.size() <= pid)
                        props.resize(pid + 1);
                    props[pid] = valueToPropertyValue(v);
                }
            }

            result.emplace_back(lid, std::move(props));
        }

        // Build __anon__ label properties if any resolved pending props exist
        if (!resolved_pending_.empty()) {
            LabelId anon_lid = getAnonLabelId(label_defs_);
            if (anon_lid != INVALID_LABEL_ID) {
                Properties anon_props;
                for (const auto& [plid, pid, expr] : resolved_pending_) {
                    if (plid != anon_lid)
                        continue;
                    Value v = evaluateExpr(evaluator, expr, chunk, row_idx);
                    if (!std::holds_alternative<std::monostate>(v)) {
                        if (anon_props.size() <= pid)
                            anon_props.resize(pid + 1);
                        anon_props[pid] = valueToPropertyValue(v);
                    }
                }
                if (!anon_props.empty())
                    result.emplace_back(anon_lid, std::move(anon_props));
            }
        }

        return result;
    };

    // Lambda: check unique constraints and insert a single vertex
    auto insertVertex = [&](VertexId vid,
                            const std::vector<std::pair<LabelId, Properties>>& label_props) -> folly::coro::Task<bool> {
        bool ok = true;
        if (!label_defs_.empty()) {
            for (const auto& [label_id, props] : label_props) {
                auto def_it = label_defs_.find(label_id);
                if (def_it == label_defs_.end())
                    continue;
                for (const auto& idx : def_it->second.indexes) {
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
                    auto table = idx.prop_ids.size() == 1 ? vidxTable(label_id, idx.prop_ids[0])
                                                          : vidxCompositeTable(label_id, idx.prop_ids);
                    bool constraint_ok = co_await store_.checkUniqueConstraint(table, values);
                    if (!constraint_ok) {
                        spdlog::warn("Unique index constraint violated on index '{}'", idx.name);
                        ok = false;
                        break;
                    }
                }
                if (!ok)
                    break;
            }
        }

        if (ok)
            ok = co_await store_.insertVertex(vid, label_props);

        if (ok && !label_defs_.empty()) {
            for (const auto& [label_id, props] : label_props) {
                auto def_it = label_defs_.find(label_id);
                if (def_it == label_defs_.end())
                    continue;
                for (const auto& idx : def_it->second.indexes) {
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
                    auto table = idx.prop_ids.size() == 1 ? vidxTable(label_id, idx.prop_ids[0])
                                                          : vidxCompositeTable(label_id, idx.prop_ids);
                    co_await store_.insertIndexEntry(table, values, vid);
                }
            }
        }
        co_return ok;
    };

    // Phase 2: per-row creation
    if (child_) {
        auto child_gen = child_->executeChunk();
        while (auto chunk = co_await child_gen.next()) {
            for (size_t row = 0; row < chunk->count; ++row) {
                VertexId vid = co_await meta_.nextVertexId();
                auto label_props = buildLabelProps(&*chunk, row);

                bool ok = co_await insertVertex(vid, label_props);
                if (ok) {
                    // Preserve child columns + append new vertex column
                    DataChunk output;
                    for (size_t c = 0; c < chunk->numColumns(); ++c) {
                        Column col = Column::flat(chunk->columns[c].type, 1);
                        col.setValue(0, chunk->getValue(c, row));
                        output.columns.push_back(std::move(col));
                    }
                    Column vertex_col = Column::flat(binder::BoundTypeKind::VERTEX, 1);
                    VertexValue vv;
                    vv.id = vid;
                    vv.labels = LabelIdSet(label_ids_.begin(), label_ids_.end());
                    for (const auto& [lid, lp] : label_props) {
                        vv.properties[lid] = lp;
                    }
                    vertex_col.setValue(0, Value(std::move(vv)));
                    output.columns.push_back(std::move(vertex_col));
                    output.count = 1;
                    co_yield std::move(output);
                }
            }
        }
    } else {
        // Standalone: create one vertex, no child columns
        VertexId vid = co_await meta_.nextVertexId();
        auto label_props = buildLabelProps(nullptr, 0);
        bool ok = co_await insertVertex(vid, label_props);
        if (ok) {
            VertexValue vv;
            vv.id = vid;
            vv.labels = LabelIdSet(label_ids_.begin(), label_ids_.end());
            for (const auto& [lid, lp] : label_props) {
                vv.properties[lid] = lp;
            }

            DataChunk output;
            output.columns.push_back(Column::flat(binder::BoundTypeKind::VERTEX, 1));
            output.columns[0].setValue(0, Value(std::move(vv)));
            output.count = 1;
            co_yield std::move(output);
        }
    }
}

} // namespace compute
} // namespace eugraph
