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

uint16_t findPropertyId_(const eugraph::LabelDef& def, const std::string& prop_name) {
    for (const auto& pd : def.properties) {
        if (pd.name == prop_name) {
            return pd.id;
        }
    }
    return UINT16_MAX;
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

void CreateNodePhysicalOp::appendPropExpr_(LabelId lid, uint16_t pid, binder::BoundExpression expr) {
    // Non-coroutine helper. BoundExpression is a variant of unique_ptrs;
    // keeping temporaries of it inside a coroutine frame triggers
    // GCC 13 ICE under -fsanitize=undefined (build_special_member_call at
    // cp/call.cc:11096). Do the move/insertion here, from a normal stack
    // frame, where GCC has no trouble.
    PropExprs single;
    single.emplace_back(pid, std::move(expr));
    label_prop_exprs_.emplace_back(lid, std::move(single));
}

folly::coro::Task<void> CreateNodePhysicalOp::prepare_() {
    // Thin dispatcher: each phase lives in its own coroutine so GCC 13's
    // coroutine-frame emission never sees a function with too much state.
    co_await prepareLabels_();
    co_await ensureTables_();
    co_await prepareAnon_();
    co_return;
}

folly::coro::Task<void> CreateNodePhysicalOp::loadLabelDef_(LabelId lid) {
    // Single-purpose helper: the optional<LabelDef> return value of
    // getLabelDefById is the most complex local we deal with. Pinning it
    // inside this tiny coroutine keeps it out of every caller's frame.
    if (lid == INVALID_LABEL_ID) {
        co_return;
    }
    auto def = co_await meta_.getLabelDefById(lid);
    if (def) {
        label_defs_[lid] = std::move(*def);
    }
    co_return;
}

folly::coro::Task<void> CreateNodePhysicalOp::createOrGetLabel_(const std::string& name, LabelId& out_lid) {
    auto existing = co_await meta_.getLabelId(name);
    if (existing) {
        out_lid = *existing;
        co_return;
    }
    LabelId lid = co_await meta_.createLabel(name, {});
    if (lid != INVALID_LABEL_ID) {
        co_await store_.createLabel(lid);
        co_await loadLabelDef_(lid);
    }
    out_lid = lid;
    co_return;
}

folly::coro::Task<void> CreateNodePhysicalOp::registerPropOnLabel_(const std::string& label_name,
                                                                   const std::string& prop_name) {
    // Isolate the temporary vector<pair<string, PropertyType>> inside its own
    // coroutine frame — letting it live in the caller's frame triggers the
    // GCC 13 build_special_member_call ICE.
    std::vector<std::pair<std::string, PropertyType>> prop_defs;
    prop_defs.emplace_back(prop_name, PropertyType::ANY);
    co_await meta_.addVertexLabelProperties(label_name, prop_defs);
    co_return;
}

folly::coro::Task<void> CreateNodePhysicalOp::resolvePropOnLabel_(LabelId lid, const std::string& prop_name,
                                                                  LabelId& out_lid, uint16_t& out_pid) {
    out_lid = INVALID_LABEL_ID;
    out_pid = UINT16_MAX;
    uint16_t pid = findPropertyId_(label_defs_[lid], prop_name);
    if (pid == UINT16_MAX) {
        co_await registerPropOnLabel_(label_defs_[lid].name, prop_name);
        co_await loadLabelDef_(lid);
        pid = findPropertyId_(label_defs_[lid], prop_name);
    }
    if (pid != UINT16_MAX) {
        out_lid = lid;
        out_pid = pid;
    }
    co_return;
}

folly::coro::Task<void> CreateNodePhysicalOp::prepareLabels_() {
    // Phase 0: Auto-create labels that were specified in the AST but were not
    // in the catalog snapshot at bind time.
    if (label_names_.empty() || label_names_resolved_.exchange(true)) {
        co_return;
    }
    spdlog::info("[CreateNode] Phase 0: auto-creating labels (label_names.size()={}, "
                 "label_ids.size()={}, pending_props.size()={})",
                 label_names_.size(), label_ids_.size(), pending_props_.size());
    std::vector<LabelId> resolved_ids;
    for (const auto& name : label_names_) {
        LabelId lid = INVALID_LABEL_ID;
        co_await createOrGetLabel_(name, lid);
        if (lid != INVALID_LABEL_ID) {
            resolved_ids.push_back(lid);
        }
    }
    if (!resolved_ids.empty() && label_ids_.size() == 1) {
        auto it = label_defs_.find(label_ids_[0]);
        bool was_anon = (it != label_defs_.end() && it->second.name == kAnonLabelName);
        if (was_anon) {
            label_ids_.clear();
        }
    }
    for (auto lid : resolved_ids) {
        if (std::find(label_ids_.begin(), label_ids_.end(), lid) == label_ids_.end())
            label_ids_.push_back(lid);
    }

    // Phase 0b: re-resolve pending_props in place — no still_pending buffer.
    if (resolved_ids.empty() || pending_props_.empty()) {
        co_return;
    }
    spdlog::info("[CreateNode] Phase 0b: re-resolving {} pending props against {} new labels", pending_props_.size(),
                 resolved_ids.size());
    size_t write_idx = 0;
    for (size_t read_idx = 0; read_idx < pending_props_.size(); ++read_idx) {
        const std::string& prop_name = pending_props_[read_idx].first;
        LabelId chosen_lid = INVALID_LABEL_ID;
        uint16_t chosen_pid = UINT16_MAX;
        for (auto lid : resolved_ids) {
            LabelId resolved_lid = INVALID_LABEL_ID;
            uint16_t resolved_pid = UINT16_MAX;
            co_await resolvePropOnLabel_(lid, prop_name, resolved_lid, resolved_pid);
            if (resolved_lid != INVALID_LABEL_ID) {
                chosen_lid = resolved_lid;
                chosen_pid = resolved_pid;
                break;
            }
        }
        if (chosen_lid != INVALID_LABEL_ID) {
            appendPropExpr_(chosen_lid, chosen_pid, std::move(pending_props_[read_idx].second));
        } else {
            if (write_idx != read_idx) {
                pending_props_[write_idx] = std::move(pending_props_[read_idx]);
            }
            ++write_idx;
        }
    }
    pending_props_.resize(write_idx);
    co_return;
}

folly::coro::Task<void> CreateNodePhysicalOp::ensureTables_() {
    // Phase 1: ensure all label data tables exist
    for (auto lid : label_ids_) {
        if (lid == INVALID_LABEL_ID)
            continue;
        if (label_defs_.find(lid) == label_defs_.end()) {
            co_await loadLabelDef_(lid);
        }
        co_await store_.createLabel(lid);
    }
    co_return;
}

folly::coro::Task<void> CreateNodePhysicalOp::prepareAnon_() {
    // Phase 1b: __anon__ attribute lightweight registration (only once).
    if (anon_registered_ || (pending_props_.empty() && !label_ids_.empty())) {
        co_return;
    }
    LabelId anon_lid = getAnonLabelId(label_defs_);
    if (anon_lid == INVALID_LABEL_ID) {
        anon_lid = co_await meta_.createLabel(std::string(kAnonLabelName), {});
        if (anon_lid != INVALID_LABEL_ID) {
            co_await store_.createLabel(anon_lid);
            co_await loadLabelDef_(anon_lid);
        }
    }
    if (anon_lid != INVALID_LABEL_ID) {
        for (auto& [prop_name, expr] : pending_props_) {
            uint16_t pid = co_await meta_.getOrCreateAnonPropId(prop_name, PropertyType::ANY);
            resolved_pending_.emplace_back(anon_lid, pid, std::move(expr));
        }
        co_await loadLabelDef_(anon_lid);
        if (label_ids_.empty()) {
            label_ids_.push_back(anon_lid);
            co_await store_.createLabel(anon_lid);
        }
    }
    anon_registered_ = true;
    co_return;
}

folly::coro::AsyncGenerator<DataChunk> CreateNodePhysicalOp::executeChunk() {
    VectorizedEvaluator evaluator(eval_ctx_);

    spdlog::info("[CreateNode] executeChunk: label_names.size()={}, label_ids.size()={}, "
                 "pending_props.size()={}",
                 label_names_.size(), label_ids_.size(), pending_props_.size());

    co_await prepare_();

    // Phase 2: per-row creation
    if (child_) {
        auto child_gen = child_->executeChunk();
        while (auto chunk = co_await child_gen.next()) {
            for (size_t row = 0; row < chunk->count; ++row) {
                VertexId vid = co_await meta_.nextVertexId();
                auto label_props = buildLabelProps(evaluator, &*chunk, row);

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
        auto label_props = buildLabelProps(evaluator, nullptr, 0);
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

std::vector<std::pair<LabelId, Properties>>
CreateNodePhysicalOp::buildLabelProps(VectorizedEvaluator& evaluator, const DataChunk* chunk, size_t row_idx) {
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
}

folly::coro::Task<bool>
CreateNodePhysicalOp::insertVertex(VertexId vid, const std::vector<std::pair<LabelId, Properties>>& label_props) {
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
}

} // namespace compute
} // namespace eugraph
