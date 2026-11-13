#include "query/physical_plan/operator/create_node_physical_op.hpp"
#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"
#include "query/dataset/row.hpp"
#include "query/evaluator/expression_evaluator.hpp"
#include "query/physical_plan/operator/property_value_convert.hpp"
#include "query/physical_plan/operator/vertex_index_maintenance.hpp"
#include <spdlog/spdlog.h>

namespace {
eugraph::LabelId getAnonLabelId(const std::unordered_map<eugraph::LabelId, eugraph::LabelDef>& label_defs) {
    for (const auto& [lid, def] : label_defs) {
        if (def.name == eugraph::kAnonLabelName)
            return lid;
    }
    return eugraph::INVALID_LABEL_ID;
}

eugraph::Value evaluateExpr(eugraph::compute::ExpressionEvaluator& evaluator,
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
    // Pending properties are NOT re-resolved against the labels here: an
    // undeclared property always belongs to the weak space (__anon__).
    // Registering it on a label here would make DML extend the declared
    // schema, which property-ownership.md forbids (it also pins the type to
    // ANY and makes `n::L.p` checks meaningless).
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
    // Phase 1b: weak-space (__anon__) registration. Every property that no
    // label of this node declares lives here — never on a label (see
    // docs/features/property-ownership.md).
    if (anon_registered_) {
        co_return;
    }
    if (pending_props_.empty() && !label_ids_.empty()) {
        anon_registered_ = true;
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
        // Always attach the hidden label when the vertex owns weak properties:
        // ProjectionExtract only prefetches __anon__ properties for vertices
        // that carry the label, so a labelled vertex with weak properties must
        // also carry it or later reads of those properties return null.
        if (std::find(label_ids_.begin(), label_ids_.end(), anon_lid) == label_ids_.end())
            label_ids_.push_back(anon_lid);
        co_await store_.createLabel(anon_lid);
    }
    anon_registered_ = true;
    co_return;
}

folly::coro::AsyncGenerator<DataChunk> CreateNodePhysicalOp::executeChunk() {
    ExpressionEvaluator evaluator(eval_ctx_);

    spdlog::info("[CreateNode] executeChunk: label_names.size()={}, label_ids.size()={}, "
                 "pending_props.size()={}",
                 label_names_.size(), label_ids_.size(), pending_props_.size());

    co_await prepare_();

    // Phase 2: per-row creation
    if (child_) {
        std::vector<const binder::BoundExpression*> prefetch_exprs;
        for (const auto& [lid, exprs] : label_prop_exprs_) {
            (void)lid;
            for (const auto& [pid, expr] : exprs) {
                (void)pid;
                prefetch_exprs.push_back(&expr);
            }
        }
        for (const auto& [lid, pid, expr] : resolved_pending_) {
            (void)lid;
            (void)pid;
            prefetch_exprs.push_back(&expr);
        }
        auto child_gen = cancellable(child_->executeChunkTraced());
        while (auto chunk = co_await child_gen.next()) {
            co_await evaluator.prefetch(prefetch_exprs, *chunk);
            // Buffer all created rows for this input chunk and yield only
            // after the whole chunk is processed. Downstream operators may
            // restart right-side scans per output chunk (CrossProduct), so
            // partial mutation visibility would change MATCH cardinality.
            DataChunk output;
            for (size_t c = 0; c < chunk->numColumns(); ++c)
                output.columns.push_back(Column::flat(chunk->columns[c].type, chunk->count));
            Column vertex_col = Column::flat(binder::BoundTypeKind::VERTEX, chunk->count);
            for (size_t row = 0; row < chunk->count; ++row) {
                VertexId vid = co_await meta_.nextVertexId();
                auto label_props = buildLabelProps(evaluator, &*chunk, row);

                bool ok = co_await insertVertex(vid, label_props);
                if (!ok)
                    continue;
                for (size_t c = 0; c < chunk->numColumns(); ++c)
                    output.columns[c].setValue(output.count, chunk->getValue(c, row));
                VertexValue vv;
                vv.id = vid;
                vv.labels = LabelIdSet(label_ids_.begin(), label_ids_.end());
                for (const auto& [lid, lp] : label_props)
                    vv.properties[lid] = lp;
                vertex_col.setValue(output.count, Value(mk<VertexValue>(std::move(vv))));
                output.count++;
            }
            output.columns.push_back(std::move(vertex_col));
            if (output.count > 0)
                co_yield std::move(output);
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
            output.columns[0].setValue(0, Value(mk<VertexValue>(std::move(vv))));
            output.count = 1;
            co_yield std::move(output);
        }
    }
}

std::vector<std::pair<LabelId, Properties>>
CreateNodePhysicalOp::buildLabelProps(ExpressionEvaluator& evaluator, const DataChunk* chunk, size_t row_idx) {
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

    return result;
}

folly::coro::Task<bool>
CreateNodePhysicalOp::insertVertex(VertexId vid, const std::vector<std::pair<LabelId, Properties>>& label_props) {
    auto planned_entries = collectVertexIndexEntriesFromLabelProps(label_defs_, label_props, vid);

    bool ok = true;
    for (const auto& entry : planned_entries) {
        if (!entry.unique)
            continue;
        bool constraint_ok = co_await store_.checkUniqueConstraint(entry.table, entry.values);
        if (!constraint_ok) {
            spdlog::warn("Unique index constraint violated while creating vertex {}", vid);
            ok = false;
            break;
        }
    }

    if (ok)
        ok = co_await store_.insertVertex(vid, label_props);

    if (ok) {
        for (const auto& entry : planned_entries)
            co_await store_.insertIndexEntry(entry.table, entry.values, entry.vid);
    }
    co_return ok;
}

} // namespace compute
} // namespace eugraph
