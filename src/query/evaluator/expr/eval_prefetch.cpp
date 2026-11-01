#include "query/evaluator/expression_evaluator.hpp"

#include "common/types/constants.hpp"
#include "query/catalog/catalog.hpp"
#include "query/planner/bound_expression/bound_dynamic_property_ref.hpp"
#include "query/planner/bound_expression/bound_expression_walk.hpp"
#include "query/planner/bound_expression/bound_property_ref.hpp"
#include "storage/data/i_async_graph_data_store.hpp"
#include "storage/meta/i_async_graph_meta_store.hpp"

#include <string>
#include <unordered_set>

namespace eugraph {
namespace compute {

namespace {

/// True when the expression contains a property access whose target may be a
/// topology-form (bare) graph entity, i.e. something the prefetch pass must
/// enrich before synchronous evaluation.
bool needsPrefetchRec(const binder::BoundExpression& expr, bool& needs_dynamic) {
    if (auto* pr = std::get_if<std::unique_ptr<binder::BoundPropertyRef>>(&expr)) {
        if (!*pr)
            return false;
        // A property ref's object is not visited by forEachSubExpr; recurse
        // explicitly so `startNode(r)[key]` style nesting is covered too.
        (void)needsPrefetchRec((*pr)->object, needs_dynamic);
        return true;
    }
    if (auto* dr = std::get_if<std::unique_ptr<binder::BoundDynamicPropertyRef>>(&expr)) {
        if (!*dr)
            return false;
        needs_dynamic = true;
        (void)needsPrefetchRec((*dr)->object, needs_dynamic);
        return true;
    }
    bool found = false;
    binder::forEachSubExpr(expr, [&](const binder::BoundExpression& child) {
        if (!found && needsPrefetchRec(child, needs_dynamic))
            found = true;
    });
    return found;
}

bool maySynthesizeRec(const binder::BoundExpression& expr) {
    if (auto* pr = std::get_if<std::unique_ptr<binder::BoundPropertyRef>>(&expr)) {
        if (*pr)
            return maySynthesizeRec((*pr)->object);
        return false;
    }
    if (auto* dr = std::get_if<std::unique_ptr<binder::BoundDynamicPropertyRef>>(&expr)) {
        if (*dr)
            return maySynthesizeRec((*dr)->object);
        return false;
    }
    if (auto* fn = std::get_if<std::unique_ptr<binder::BoundFunctionCall>>(&expr)) {
        if (*fn && (*fn)->func_def) {
            const auto& name = (*fn)->func_def->name;
            // startNode/endNode construct a fresh bare VertexValue from an
            // edge's endpoint id; that vertex need not appear in the chunk.
            if (name == "startNode" || name == "endNode")
                return true;
        }
    }
    bool found = false;
    binder::forEachSubExpr(expr, [&](const binder::BoundExpression& child) {
        if (!found && maySynthesizeRec(child))
            found = true;
    });
    return found;
}

} // namespace

bool ExpressionEvaluator::expressionMaySynthesizeEntity(const binder::BoundExpression& expr) {
    return maySynthesizeRec(expr);
}

bool ExpressionEvaluator::expressionNeedsPrefetch(const binder::BoundExpression& expr) {
    bool needs_dynamic = false;
    return needsPrefetchRec(expr, needs_dynamic);
}

uint64_t ExpressionEvaluator::missingPrefetchCount() noexcept {
    return missing_prefetch_count_.load(std::memory_order_relaxed);
}

void ExpressionEvaluator::resetMissingPrefetchCount() noexcept {
    missing_prefetch_count_.store(0, std::memory_order_relaxed);
}

const LabelDef* ExpressionEvaluator::findLabelDef(LabelId label_id) const {
    if (eval_ctx_.label_defs) {
        auto it = eval_ctx_.label_defs->find(label_id);
        if (it != eval_ctx_.label_defs->end())
            return &it->second;
    }
    if (eval_ctx_.catalog) {
        if (auto* def = eval_ctx_.catalog->lookupLabel(label_id))
            return def;
    }
    for (const auto& def : label_def_cache_) {
        if (def && def->id == label_id)
            return def.get();
    }
    return nullptr;
}

const EdgeLabelDef* ExpressionEvaluator::findEdgeLabelDef(EdgeLabelId label_id) const {
    if (eval_ctx_.edge_label_defs) {
        auto it = eval_ctx_.edge_label_defs->find(label_id);
        if (it != eval_ctx_.edge_label_defs->end())
            return &it->second;
    }
    if (eval_ctx_.catalog) {
        if (auto* def = eval_ctx_.catalog->lookupEdgeLabel(label_id))
            return def;
    }
    for (const auto& def : edge_label_def_cache_) {
        if (def && def->id == label_id)
            return def.get();
    }
    return nullptr;
}

folly::coro::Task<void> ExpressionEvaluator::ensureLabelDefs(const std::unordered_set<LabelId>& label_ids,
                                                             const std::unordered_set<EdgeLabelId>& edge_label_ids) {
    if (eval_ctx_.meta) {
        for (LabelId id : label_ids) {
            if (findLabelDef(id))
                continue;
            auto def = co_await eval_ctx_.meta->getLabelDefById(id);
            if (def)
                label_def_cache_.push_back(std::make_unique<LabelDef>(std::move(*def)));
        }
        for (EdgeLabelId id : edge_label_ids) {
            if (findEdgeLabelDef(id))
                continue;
            auto def = co_await eval_ctx_.meta->getEdgeLabelDefById(id);
            if (def)
                edge_label_def_cache_.push_back(std::make_unique<EdgeLabelDef>(std::move(*def)));
        }
    }
}

folly::coro::Task<void> ExpressionEvaluator::loadVertexDataAndDefs(const std::vector<VertexId>& ids) {
    if (ids.empty() || !eval_ctx_.store)
        co_return;
    std::vector<VertexId> missing;
    missing.reserve(ids.size());
    std::unordered_set<VertexId> seen;
    for (VertexId id : ids) {
        if (prefetched_vertex_data_.count(id) == 0 && seen.insert(id).second)
            missing.push_back(id);
    }
    if (missing.empty())
        co_return;
    auto batch = co_await eval_ctx_.store->batchGetVertexData(missing);
    prefetched_vertex_data_.reserve(prefetched_vertex_data_.size() + missing.size());
    for (size_t i = 0; i < missing.size(); ++i) {
        PrefetchedVertexData data;
        if (i < batch.size()) {
            data.labels = std::move(batch[i].labels);
            data.properties = std::move(batch[i].properties);
        }
        prefetched_vertex_data_.emplace(missing[i], std::move(data));
    }
}

folly::coro::Task<void> ExpressionEvaluator::prefetch(const std::vector<const binder::BoundExpression*>& exprs,
                                                      const DataChunk& input) {
    bool needs_dynamic = false;
    bool needed = false;
    for (const auto* expr : exprs) {
        if (expr && needsPrefetchRec(*expr, needs_dynamic)) {
            needed = true;
            // Keep walking: a later expression may need the dynamic path even
            // when this one only needs the static one.
        }
    }
    if (!needed || input.numRows() == 0)
        co_return;

    // The cache is additive for the evaluator's lifetime: operators reuse it
    // across rows (and Merge prefetches a synthesized chunk without losing the
    // input chunk's entries). Graph data is immutable within a statement's
    // read snapshot; write operators only read values they have not mutated.

    std::vector<VertexId> ids;
    std::unordered_set<VertexId> seen;
    std::unordered_set<LabelId> label_ids;
    std::unordered_set<EdgeLabelId> edge_label_ids;

    auto note_labels = [&label_ids](const VertexValue& vertex) {
        if (vertex.labels) {
            for (LabelId id : *vertex.labels)
                label_ids.insert(id);
        }
        for (const auto& [id, props] : vertex.properties) {
            (void)props;
            label_ids.insert(id);
        }
    };

    // Walk every value in the chunk. Values are acyclic; lists, maps and paths
    // are traversed so `nodes(p)`, `[...]` and map literals are covered as well
    // as direct vertex columns.
    auto scan = [&](auto&& self, const Value& value) -> void {
        std::visit(
            [&](const auto& alt) {
                using T = std::decay_t<decltype(alt)>;
                if constexpr (std::is_same_v<T, VertexValuePtr>) {
                    if (!alt)
                        return;
                    note_labels(*alt);
                    if (alt->properties.empty() && seen.insert(alt->id).second)
                        ids.push_back(alt->id);
                } else if constexpr (std::is_same_v<T, EdgeValuePtr>) {
                    if (!alt)
                        return;
                    if (alt->properties.has_value())
                        edge_label_ids.insert(alt->label_id);
                } else if constexpr (std::is_same_v<T, ListValuePtr>) {
                    if (alt) {
                        for (const auto& elem : alt->elements)
                            self(self, elem.value);
                    }
                } else if constexpr (std::is_same_v<T, MapValuePtr>) {
                    if (alt) {
                        for (const auto& [key, storage] : alt->entries) {
                            (void)key;
                            self(self, storage.value);
                        }
                    }
                } else if constexpr (std::is_same_v<T, PathValuePtr>) {
                    if (alt) {
                        for (const auto& elem : alt->elements)
                            self(self, elem.value);
                    }
                }
            },
            value);
    };
    for (size_t c = 0; c < input.numColumns(); ++c) {
        for (size_t r = 0; r < input.numRows(); ++r) {
            Value value = input.getValue(c, r);
            scan(scan, value);
        }
    }

    co_await loadVertexDataAndDefs(ids);
    for (VertexId id : ids) {
        auto it = prefetched_vertex_data_.find(id);
        if (it == prefetched_vertex_data_.end())
            continue;
        for (LabelId label_id : it->second.labels)
            label_ids.insert(label_id);
        for (const auto& [label_id, props] : it->second.properties) {
            (void)props;
            label_ids.insert(label_id);
        }
    }
    co_await ensureLabelDefs(label_ids, edge_label_ids);

    // Dry-pass fallback for values synthesized during evaluation (startNode /
    // endNode of an edge whose endpoints are not otherwise columns). Evaluate
    // once with recording enabled, batch-load what it needs, then let the real
    // evaluation run against the cache.
    for (const auto* expr : exprs) {
        if (!expr || !expressionMaySynthesizeEntity(*expr))
            continue;
        for (int pass = 0; pass < 3; ++pass) {
            prefetch_pending_ids_.clear();
            Column scratch = Column::flat(binder::getBoundExprType(*expr).kind, input.numRows());
            prefetching_ = true;
            try {
                evaluate(*expr, input, scratch);
            } catch (...) {
                prefetching_ = false;
                throw;
            }
            prefetching_ = false;
            if (prefetch_pending_ids_.empty())
                break;
            std::vector<VertexId> pending;
            pending.swap(prefetch_pending_ids_);
            co_await loadVertexDataAndDefs(pending);
            std::unordered_set<LabelId> extra_labels;
            for (VertexId id : pending) {
                auto it = prefetched_vertex_data_.find(id);
                if (it == prefetched_vertex_data_.end())
                    continue;
                for (LabelId label_id : it->second.labels)
                    extra_labels.insert(label_id);
                for (const auto& [label_id, props] : it->second.properties) {
                    (void)props;
                    extra_labels.insert(label_id);
                }
            }
            co_await ensureLabelDefs(extra_labels, {});
        }
    }

    // The __anon__ bypass is only interesting for dynamic property access.
    // Resolve it once per evaluator, asynchronously, so evaluation never has to.
    if (needs_dynamic && !anon_label_resolved_ && eval_ctx_.meta) {
        anon_label_resolved_ = true;
        auto def = co_await eval_ctx_.meta->getLabelDef(std::string(kAnonLabelName));
        if (def)
            anon_label_id_cached_ = def->id;
    }
}

} // namespace compute
} // namespace eugraph
