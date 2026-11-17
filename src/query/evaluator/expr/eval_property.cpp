#include "query/evaluator/expression_evaluator.hpp"

#include <algorithm>
#include <limits>
#include <optional>

#include "common/types/constants.hpp"
#include "common/types/property_home.hpp"
#include "storage/storage_op_metrics.hpp"

#include "query/catalog/catalog.hpp"
#include "query/planner/bound_expression/bound_dynamic_property_ref.hpp"
#include "storage/data/i_async_graph_data_store.hpp"
#include "storage/meta/i_async_graph_meta_store.hpp"

namespace eugraph {
namespace {
/// Counters a compute-side prefetch event is charged to: the statement's
/// current operator counters (no TLS, see StorageProfileState).
inline StorageOpCounters* currentCounters(const function::EvalContext& ctx) {
    return ctx.store != nullptr ? ctx.store->currentStorageCounters() : nullptr;
}
} // namespace

namespace compute {

void ExpressionEvaluator::evalPropertyRef(const binder::BoundPropertyRef& ref, const DataChunk& input, Column& result,
                                          size_t count) {
    auto obj = evaluateInternal(ref.object, input);
    if (!obj.column)
        return;

    auto pvToValue = [](const PropertyValue& pv) -> Value {
        if (std::holds_alternative<bool>(pv))
            return Value(std::get<bool>(pv));
        if (std::holds_alternative<int64_t>(pv))
            return Value(std::get<int64_t>(pv));
        if (std::holds_alternative<double>(pv))
            return Value(std::get<double>(pv));
        if (std::holds_alternative<std::string>(pv))
            return Value(std::get<std::string>(pv));
        if (std::holds_alternative<DateTimeValue>(pv))
            return Value(std::get<DateTimeValue>(pv));
        if (std::holds_alternative<TimeValue>(pv))
            return Value(std::get<TimeValue>(pv));
        if (std::holds_alternative<DurationValue>(pv))
            return Value(std::get<DurationValue>(pv));
        if (std::holds_alternative<std::vector<uint8_t>>(pv))
            return Value(mk<BytesValue>(std::get<std::vector<uint8_t>>(pv)));
        if (std::holds_alternative<std::vector<int64_t>>(pv)) {
            ListValue lv;
            for (auto v : std::get<std::vector<int64_t>>(pv))
                lv.elements.push_back(ValueStorage{Value(v)});
            return Value(mk<ListValue>(std::move(lv)));
        }
        if (std::holds_alternative<std::vector<double>>(pv)) {
            ListValue lv;
            for (auto v : std::get<std::vector<double>>(pv))
                lv.elements.push_back(ValueStorage{Value(v)});
            return Value(mk<ListValue>(std::move(lv)));
        }
        if (std::holds_alternative<std::vector<std::string>>(pv)) {
            ListValue lv;
            for (auto& s : std::get<std::vector<std::string>>(pv))
                lv.elements.push_back(ValueStorage{Value(std::move(s))});
            return Value(mk<ListValue>(std::move(lv)));
        }
        if (std::holds_alternative<std::vector<DateTimeValue>>(pv)) {
            ListValue lv;
            for (const auto& v : std::get<std::vector<DateTimeValue>>(pv))
                lv.elements.push_back(ValueStorage{Value(v)});
            return Value(mk<ListValue>(std::move(lv)));
        }
        if (std::holds_alternative<std::vector<TimeValue>>(pv)) {
            ListValue lv;
            for (const auto& v : std::get<std::vector<TimeValue>>(pv))
                lv.elements.push_back(ValueStorage{Value(v)});
            return Value(mk<ListValue>(std::move(lv)));
        }
        if (std::holds_alternative<std::vector<DurationValue>>(pv)) {
            ListValue lv;
            for (const auto& v : std::get<std::vector<DurationValue>>(pv))
                lv.elements.push_back(ValueStorage{Value(v)});
            return Value(mk<ListValue>(std::move(lv)));
        }
        return Value{};
    };

    LabelId anon_id = INVALID_LABEL_ID;
    if (eval_ctx_.catalog) {
        anon_id = eval_ctx_.catalog->getAnonLabelId();
    } else if (eval_ctx_.label_defs) {
        for (const auto& [lid, def] : *eval_ctx_.label_defs) {
            if (def.name == kAnonLabelName) {
                anon_id = lid;
                break;
            }
        }
    }
    // Candidate-based weak resolution: the binder already resolved every label
    // that declares the property, so counting homes is O(candidates) and does
    // not scan label schemas or allocate per row (ASan builds were paying a
    // large allocation tax for the old vector-of-names path).
    uint16_t anon_pid_from_def = UINT16_MAX;
    if (anon_id != INVALID_LABEL_ID) {
        const LabelDef* adef = findLabelDef(anon_id);
        if (adef) {
            const PropertyDef* apd = findPropertyDefByName(*adef, ref.property_name);
            if (apd)
                anon_pid_from_def = apd->id;
        }
    }
    auto resolveWeak = [&](const LabelIdSet& held, const std::unordered_map<LabelId, Properties>& props,
                           const Properties* anon_override) -> std::optional<PropertyValue> {
        int homes = 0;
        std::optional<PropertyValue> value;
        for (const auto& cand : ref.candidates) {
            if (cand.label_id == anon_id || cand.label_id == INVALID_LABEL_ID)
                continue;
            if (held.count(cand.label_id) == 0)
                continue;
            ++homes;
            auto pit = props.find(cand.label_id);
            if (pit != props.end() && cand.prop_id < pit->second.size() && pit->second[cand.prop_id].has_value())
                value = pit->second[cand.prop_id].value();
        }
        uint16_t anon_pid = UINT16_MAX;
        for (const auto& cand : ref.candidates) {
            if (cand.label_id == anon_id) {
                anon_pid = cand.prop_id;
                break;
            }
        }
        if (anon_pid == UINT16_MAX)
            anon_pid = anon_pid_from_def;
        if (anon_id != INVALID_LABEL_ID && anon_pid != UINT16_MAX && held.count(anon_id) != 0) {
            const Properties* ap = nullptr;
            auto it = props.find(anon_id);
            if (it != props.end())
                ap = &it->second;
            else if (anon_override)
                ap = anon_override;
            if (ap && anon_pid < ap->size() && (*ap)[anon_pid].has_value()) {
                ++homes;
                value = (*ap)[anon_pid].value();
            }
        }
        if (homes > 1) {
            throw std::runtime_error("AmbiguousProperty: property '" + ref.property_name +
                                     "' exists in multiple places on this vertex; use n::<Label>." + ref.property_name +
                                     " to select one");
        }
        return value;
    };

    for (size_t i = 0; i < count; ++i) {
        Value ov = obj.column->getValue(i);
        Value r;
        if (std::holds_alternative<VertexValuePtr>(ov)) {
            const auto& held = std::get<VertexValuePtr>(ov);
            if (!held)
                continue;
            const auto& vertex = *held;
            if (vertex.deleted)
                throw std::runtime_error("EntityNotFound: DeletedEntityAccess");
            // Topology-form vertices (shortest-path BFS, startNode/endNode of
            // a path) only carry the internal id. The data was loaded by
            // prefetch(); evaluation must never touch storage itself.
            const std::unordered_map<LabelId, Properties>* props = &vertex.properties;
            if (vertex.properties.empty() && eval_ctx_.store) {
                auto pit = prefetched_vertex_data_.find(vertex.id);
                if (pit != prefetched_vertex_data_.end()) {
                    props = &pit->second.properties;
                    recordPrefetchHit(currentCounters(eval_ctx_));
                } else if (prefetching_) {
                    prefetch_pending_ids_.push_back(vertex.id);
                } else {
                    missing_prefetch_count_.fetch_add(1, std::memory_order_relaxed);
                    recordPrefetchMiss(currentCounters(eval_ctx_));
                }
            }
            // Strong access reads ONLY the cast label's declared column.
            // Weak access resolves the property's "homes": each held label that
            // declares it, plus __anon__ when it holds a value. More than one
            // home is ambiguity (docs/features/strong-weak-hybrid.md §4).
            if (ref.strong_mode && ref.candidates.size() == 1) {
                const auto& cand = ref.candidates[0];
                auto lit = props->find(cand.label_id);
                if (lit != props->end() && cand.prop_id < lit->second.size() && lit->second[cand.prop_id].has_value())
                    r = pvToValue(*lit->second[cand.prop_id]);
            } else {
                LabelIdSet derived;
                const LabelIdSet* held = nullptr;
                if (vertex.labels.has_value()) {
                    held = &*vertex.labels;
                } else {
                    for (const auto& [lid, _] : *props)
                        derived.insert(lid);
                    held = &derived;
                }
                if (auto value = resolveWeak(*held, *props, nullptr); value.has_value())
                    r = pvToValue(*value);
            }
        } else if (std::holds_alternative<VertexRef>(ov)) {
            const auto& vref = std::get<VertexRef>(ov);
            // Structural field access on topology-form vertex (n.id).
            // The whole-vertex object hasn't been constructed (no PEPlan
            // requested it), so handle the cheap structural reads here.
            if (ref.candidates.empty() && !ref.property_name.empty() && ref.property_name == "id") {
                r = Value(static_cast<int64_t>(vref.id));
            } else if (!ref.property_name.empty()) {
                // A property access on a bare VertexRef (e.g. VarLenExpand's
                // inline edge filter {score: a.name}) reads the data loaded by
                // prefetch(); evaluation stays storage-free.
                auto pit = prefetched_vertex_data_.find(vref.id);
                if (pit != prefetched_vertex_data_.end()) {
                    recordPrefetchHit(currentCounters(eval_ctx_));
                    const auto& props = pit->second.properties;
                    if (ref.strong_mode && ref.candidates.size() == 1) {
                        const auto& cand = ref.candidates[0];
                        auto lit = props.find(cand.label_id);
                        if (lit != props.end() && cand.prop_id < lit->second.size() &&
                            lit->second[cand.prop_id].has_value())
                            r = pvToValue(*lit->second[cand.prop_id]);
                    } else {
                        if (auto value = resolveWeak(pit->second.labels, props, nullptr); value.has_value())
                            r = pvToValue(*value);
                    }
                } else if (prefetching_) {
                    prefetch_pending_ids_.push_back(vref.id);
                } else {
                    missing_prefetch_count_.fetch_add(1, std::memory_order_relaxed);
                    recordPrefetchMiss(currentCounters(eval_ctx_));
                }
            }
        } else if (std::holds_alternative<EdgeKey>(ov)) {
            const auto& ek = std::get<EdgeKey>(ov);
            // Structural field access on topology-form edge (r.id, r.src_id,
            // r.dst_id, r.label_id). Avoids forcing ConstructEdge when only
            // the structural id is needed.
            if (ref.candidates.empty() && !ref.property_name.empty()) {
                if (ref.property_name == "id") {
                    r = Value(static_cast<int64_t>(ek.id));
                } else if (ref.property_name == "src_id") {
                    r = Value(static_cast<int64_t>(ek.src_id));
                } else if (ref.property_name == "dst_id") {
                    r = Value(static_cast<int64_t>(ek.dst_id));
                } else if (ref.property_name == "label_id") {
                    r = Value(static_cast<int64_t>(ek.label_id));
                }
            }
        } else if (std::holds_alternative<EdgeValuePtr>(ov)) {
            const auto& edge = (*std::get<EdgeValuePtr>(ov));
            if (edge.deleted)
                throw std::runtime_error("EntityNotFound: DeletedEntityAccess");
            // When the property wasn't resolved at bind time (candidates empty),
            // look it up by name at runtime. User-defined properties take
            // precedence over structural fields — otherwise a user property
            // named "id" would always be shadowed by the internal edge id.
            if (ref.candidates.empty() && !ref.property_name.empty()) {
                bool resolved = false;
                if (edge.properties.has_value()) {
                    const EdgeLabelDef* eldef = findEdgeLabelDef(edge.label_id);
                    if (eldef) {
                        for (const auto& pd : eldef->properties) {
                            if (pd.name == ref.property_name && pd.id < edge.properties->size()) {
                                const auto& pv = (*edge.properties)[pd.id];
                                if (pv.has_value()) {
                                    r = pvToValue(*pv);
                                    resolved = true;
                                }
                                break;
                            }
                        }
                    }
                }
                // Fall back to structural fields only if no user property matches.
                if (!resolved) {
                    if (ref.property_name == "id") {
                        r = Value(static_cast<int64_t>(edge.id));
                    } else if (ref.property_name == "src_id") {
                        r = Value(static_cast<int64_t>(edge.src_id));
                    } else if (ref.property_name == "dst_id") {
                        r = Value(static_cast<int64_t>(edge.dst_id));
                    } else if (ref.property_name == "label_id") {
                        r = Value(static_cast<int64_t>(edge.label_id));
                    }
                }
            } else if (edge.properties.has_value()) {
                for (const auto& candidate : ref.candidates) {
                    if (candidate.prop_id < edge.properties->size()) {
                        const auto& pv = (*edge.properties)[candidate.prop_id];
                        if (pv.has_value()) {
                            r = pvToValue(*pv);
                            break;
                        }
                    }
                }
            }
        }
        result.setValue(i, r);
    }
}

void ExpressionEvaluator::evalDynamicPropertyRef(const binder::BoundDynamicPropertyRef& ref, const DataChunk& input,
                                                 Column& result, size_t count) {
    auto obj = evaluateInternal(ref.object, input);
    if (!obj.column)
        return;

    auto pvToValue = [](const PropertyValue& pv) -> Value {
        if (std::holds_alternative<bool>(pv))
            return Value(std::get<bool>(pv));
        if (std::holds_alternative<int64_t>(pv))
            return Value(std::get<int64_t>(pv));
        if (std::holds_alternative<double>(pv))
            return Value(std::get<double>(pv));
        if (std::holds_alternative<std::string>(pv))
            return Value(std::get<std::string>(pv));
        if (std::holds_alternative<DateTimeValue>(pv))
            return Value(std::get<DateTimeValue>(pv));
        if (std::holds_alternative<TimeValue>(pv))
            return Value(std::get<TimeValue>(pv));
        if (std::holds_alternative<DurationValue>(pv))
            return Value(std::get<DurationValue>(pv));
        if (std::holds_alternative<std::vector<uint8_t>>(pv))
            return Value(mk<BytesValue>(std::get<std::vector<uint8_t>>(pv)));
        if (std::holds_alternative<std::vector<int64_t>>(pv)) {
            ListValue lv;
            for (auto v : std::get<std::vector<int64_t>>(pv))
                lv.elements.push_back(ValueStorage{Value(v)});
            return Value(mk<ListValue>(std::move(lv)));
        }
        if (std::holds_alternative<std::vector<double>>(pv)) {
            ListValue lv;
            for (auto v : std::get<std::vector<double>>(pv))
                lv.elements.push_back(ValueStorage{Value(v)});
            return Value(mk<ListValue>(std::move(lv)));
        }
        if (std::holds_alternative<std::vector<std::string>>(pv)) {
            ListValue lv;
            for (auto& s : std::get<std::vector<std::string>>(pv))
                lv.elements.push_back(ValueStorage{Value(std::move(s))});
            return Value(mk<ListValue>(std::move(lv)));
        }
        if (std::holds_alternative<std::vector<DateTimeValue>>(pv)) {
            ListValue lv;
            for (const auto& v : std::get<std::vector<DateTimeValue>>(pv))
                lv.elements.push_back(ValueStorage{Value(v)});
            return Value(mk<ListValue>(std::move(lv)));
        }
        if (std::holds_alternative<std::vector<TimeValue>>(pv)) {
            ListValue lv;
            for (const auto& v : std::get<std::vector<TimeValue>>(pv))
                lv.elements.push_back(ValueStorage{Value(v)});
            return Value(mk<ListValue>(std::move(lv)));
        }
        if (std::holds_alternative<std::vector<DurationValue>>(pv)) {
            ListValue lv;
            for (const auto& v : std::get<std::vector<DurationValue>>(pv))
                lv.elements.push_back(ValueStorage{Value(v)});
            return Value(mk<ListValue>(std::move(lv)));
        }
        return Value{};
    };

    constexpr uint16_t kMissingProp = std::numeric_limits<uint16_t>::max();
    auto propIdFor = [&](LabelId label_id) -> uint16_t {
        auto& index = dyn_prop_index_cache_[label_id];
        auto hit = index.find(ref.property);
        if (hit != index.end())
            return hit->second;

        const LabelDef* ldef = nullptr;
        if (eval_ctx_.label_defs) {
            auto it = eval_ctx_.label_defs->find(label_id);
            if (it != eval_ctx_.label_defs->end())
                ldef = &it->second;
        }
        if (!ldef)
            ldef = eval_ctx_.catalog->lookupLabel(label_id);

        if (ldef) {
            for (const auto& pd : ldef->properties) {
                if (pd.name == ref.property) {
                    index.emplace(ref.property, pd.id);
                    return pd.id;
                }
            }
            return kMissingProp;
        }

        if (const auto* loaded = findLabelDef(label_id)) {
            for (const auto& pd : loaded->properties) {
                if (pd.name == ref.property) {
                    index.emplace(ref.property, pd.id);
                    return pd.id;
                }
            }
        }
        return kMissingProp;
    };

    for (size_t i = 0; i < count; ++i) {
        Value ov;
        // The fast path reads the column's handle directly, so it has to check both
        // bounds and emptiness itself: reserve() leaves a slot without a payload.
        if (obj.column->form == VectorForm::FLAT && obj.column->buffer && i < obj.column->buffer->vertex_data.size() &&
            obj.column->buffer->vertex_data[i])
            ov = obj.column->buffer->vertex_data[i];
        else
            ov = obj.column->getValue(i);
        Value r;
        if (std::holds_alternative<VertexValuePtr>(ov)) {
            const auto& held = std::get<VertexValuePtr>(ov);
            if (!held)
                continue;
            // Read-only view. This loop runs once per row; cloning the shared
            // VertexValue here (its Properties map can hold vector<string>
            // values such as Person.speaks) dominated complex-14's PC
            // predicate. Owned properties win; topology-form entities
            // (startNode/endNode, path elements) read the prefetch cache.
            const VertexValue& vertex = *held;
            if (vertex.deleted)
                throw std::runtime_error("EntityNotFound: DeletedEntityAccess");
            const std::unordered_map<LabelId, Properties>* props = &vertex.properties;
            if (vertex.properties.empty() && eval_ctx_.store) {
                auto pit = prefetched_vertex_data_.find(vertex.id);
                if (pit != prefetched_vertex_data_.end()) {
                    props = &pit->second.properties;
                    recordPrefetchHit(currentCounters(eval_ctx_));
                } else if (prefetching_) {
                    prefetch_pending_ids_.push_back(vertex.id);
                } else {
                    missing_prefetch_count_.fetch_add(1, std::memory_order_relaxed);
                    recordPrefetchMiss(currentCounters(eval_ctx_));
                }
            }
            // __anon__ may exist only in the prefetch cache after REMOVE LABEL
            // migrated the property. Read it from there instead of merging into
            // a copy: (a) the vertex may have no user label left, (b) it may
            // still have labels while the property copy lives under __anon__.
            const LabelId anon_lid = anon_label_id_cached_;
            const Properties* anon_props = nullptr;
            if (eval_ctx_.meta && anon_lid != INVALID_LABEL_ID && props->find(anon_lid) == props->end()) {
                auto pit = prefetched_vertex_data_.find(vertex.id);
                if (pit != prefetched_vertex_data_.end()) {
                    auto anon_it = pit->second.properties.find(anon_lid);
                    if (anon_it != pit->second.properties.end())
                        anon_props = &anon_it->second;
                }
            }

            // Held labels: prefer the loaded set; fall back to the materialized
            // property map (topology-form entities may not carry labels).
            LabelIdSet derived_labels;
            const LabelIdSet* held_ptr = nullptr;
            if (vertex.labels.has_value()) {
                held_ptr = &*vertex.labels;
            } else {
                for (const auto& [lid, _] : *props)
                    derived_labels.insert(lid);
                if (anon_props)
                    derived_labels.insert(anon_lid);
                held_ptr = &derived_labels;
            }
            const LabelIdSet& held_labels = *held_ptr;
            auto lookup_def = [&](LabelId lid) -> const LabelDef* {
                const LabelDef* def = findLabelDef(lid);
                if (!def && eval_ctx_.catalog)
                    def = eval_ctx_.catalog->lookupLabel(lid);
                return def;
            };
            auto declares = [&](const LabelDef* def, const std::string& name) -> bool {
                if (!def)
                    return false;
                for (const auto& pd : def->properties) {
                    if (pd.name == name)
                        return true;
                }
                return false;
            };
            auto read_value = [&](const LabelDef* def, const Properties& props_vec,
                                  std::optional<PropertyValue>& out) -> bool {
                if (!def)
                    return false;
                for (const auto& pd : def->properties) {
                    if (pd.name != ref.property)
                        continue;
                    if (pd.id < props_vec.size() && props_vec[pd.id].has_value()) {
                        out = props_vec[pd.id].value();
                        return true;
                    }
                    return false;
                }
                return false;
            };

            // Same home rule as n.p: count declaring labels (value irrelevant)
            // plus an anon value; 2+ homes is ambiguous.
            int homes = 0;
            std::optional<PropertyValue> home_value;
            for (LabelId lid : held_labels) {
                if (lid == anon_lid)
                    continue;
                auto pit = props->find(lid);
                if (pit == props->end())
                    continue;
                const LabelDef* def = lookup_def(lid);
                if (!declares(def, ref.property))
                    continue;
                homes++;
                read_value(def, pit->second, home_value);
            }
            if (anon_lid != INVALID_LABEL_ID) {
                const Properties* ap = nullptr;
                auto ait = props->find(anon_lid);
                if (ait != props->end())
                    ap = &ait->second;
                else if (anon_props)
                    ap = anon_props;
                const LabelDef* def = lookup_def(anon_lid);
                std::optional<PropertyValue> anon_value;
                if (ap && read_value(def, *ap, anon_value)) {
                    homes++;
                    home_value = anon_value;
                }
            }
            if (homes > 1) {
                throw std::runtime_error("AmbiguousProperty: property '" + ref.property +
                                         "' exists in multiple places on this vertex; use n::<Label>." + ref.property +
                                         " to select one");
            }
            if (homes == 1) {
                if (home_value.has_value())
                    r = pvToValue(*home_value);
            } else if (eval_ctx_.meta) {
                // Fallback: DDL may have created the property mid-query after
                // the static LabelDef snapshot was taken. Rescan fresh defs and
                // still reject multiple homes.
                int fresh_homes = 0;
                std::optional<PropertyValue> fresh_value;
                for (LabelId lid : held_labels) {
                    auto pit = props->find(lid);
                    if (pit == props->end())
                        continue;
                    const LabelDef* def = lookup_def(lid);
                    if (!declares(def, ref.property))
                        continue;
                    fresh_homes++;
                    read_value(def, pit->second, fresh_value);
                }
                if (fresh_homes > 1) {
                    throw std::runtime_error("AmbiguousProperty: property '" + ref.property +
                                             "' exists in multiple places on this vertex; use n::<Label>." +
                                             ref.property + " to select one");
                }
                if (fresh_homes == 1 && fresh_value.has_value())
                    r = pvToValue(*fresh_value);
            }
        } else if (std::holds_alternative<EdgeValuePtr>(ov)) {
            const auto& edge = (*std::get<EdgeValuePtr>(ov));
            if (edge.deleted)
                throw std::runtime_error("EntityNotFound: DeletedEntityAccess");
            if (ref.property == "id") {
                r = Value(static_cast<int64_t>(edge.id));
            } else if (ref.property == "src_id") {
                r = Value(static_cast<int64_t>(edge.src_id));
            } else if (ref.property == "dst_id") {
                r = Value(static_cast<int64_t>(edge.dst_id));
            } else if (ref.property == "label_id") {
                r = Value(static_cast<int64_t>(edge.label_id));
            } else if (edge.properties.has_value()) {
                const auto& props = *edge.properties;
                if (eval_ctx_.catalog) {
                    const auto* eldef = eval_ctx_.catalog->lookupEdgeLabel(edge.label_id);
                    if (eldef) {
                        for (const auto& pd : eldef->properties) {
                            if (pd.name == ref.property && pd.id < props.size()) {
                                const auto& pv = props[pd.id];
                                if (pv.has_value())
                                    r = pvToValue(*pv);
                                break;
                            }
                        }
                    }
                }
            }
        } else if (std::holds_alternative<MapValuePtr>(ov)) {
            const auto& mv = (*std::get<MapValuePtr>(ov));
            for (const auto& [key, storage] : mv.entries) {
                if (key == ref.property) {
                    r = storage.value;
                    break;
                }
            }
        }
        result.setValue(i, r);
    }
}

} // namespace compute
} // namespace eugraph
