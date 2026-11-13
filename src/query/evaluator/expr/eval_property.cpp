#include "query/evaluator/expression_evaluator.hpp"

#include <algorithm>
#include <optional>

#include "common/types/constants.hpp"
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
            // Weak access resolves by name across every source. All sources
            // must agree on one value; a conflict is an error, never a list
            // (docs/features/property-ownership.md §4). Keep the common case
            // allocation-free and only build the label list on a conflict.
            std::optional<PropertyValue> chosen;
            std::vector<std::string> seen_labels;
            auto consider = [&](const PropertyValue& pv, LabelId lid) {
                if (!chosen) {
                    chosen = pv;
                    return;
                }
                if (*chosen == pv)
                    return; // equal duplicates are harmless
                const LabelDef* ldef = findLabelDef(lid);
                std::string label_name = ldef ? ldef->name : std::to_string(lid);
                if (std::find(seen_labels.begin(), seen_labels.end(), label_name) == seen_labels.end())
                    seen_labels.push_back(label_name);
                std::string labels;
                for (const auto& n : seen_labels) {
                    if (!labels.empty())
                        labels += ", ";
                    labels += n;
                }
                throw std::runtime_error("AmbiguousProperty: property '" + ref.property_name +
                                         "' has conflicting values in labels (" + labels + "); use n::<Label>." +
                                         ref.property_name + " to select a label scope");
            };
            for (const auto& candidate : ref.candidates) {
                auto lit = props->find(candidate.label_id);
                if (lit == props->end())
                    continue;
                const auto& candidate_props = lit->second;
                if (candidate.prop_id >= candidate_props.size() || !candidate_props[candidate.prop_id].has_value())
                    continue;
                const LabelDef* ldef = findLabelDef(candidate.label_id);
                if (ldef) {
                    auto it = std::find(seen_labels.begin(), seen_labels.end(), ldef->name);
                    if (it == seen_labels.end())
                        seen_labels.push_back(ldef->name);
                }
                consider(*candidate_props[candidate.prop_id], candidate.label_id);
            }
            // Fallback: only the weak space may answer a name-based lookup when
            // none of the resolved candidates matched. Searching every loaded
            // label here would let `n::Person.name` silently return another
            // label's same-named value, defeating the cast.
            if (!chosen && !ref.property_name.empty()) {
                for (const auto& [lid, props_vec] : *props) {
                    const LabelDef* ldef = findLabelDef(lid);
                    if (!ldef || ldef->name != kAnonLabelName)
                        continue;
                    for (const auto& pd : ldef->properties) {
                        if (pd.name == ref.property_name && pd.id < props_vec.size()) {
                            const auto& pv = props_vec[pd.id];
                            if (pv.has_value()) {
                                auto it = std::find(seen_labels.begin(), seen_labels.end(), ldef->name);
                                if (it == seen_labels.end())
                                    seen_labels.push_back(ldef->name);
                                consider(*pv, lid);
                                break;
                            }
                        }
                    }
                    if (chosen)
                        break;
                }
            }
            if (chosen)
                r = pvToValue(*chosen);
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
                    std::optional<PropertyValue> chosen;
                    std::vector<std::string> value_labels;
                    auto consider = [&](const PropertyValue& pv, LabelId lid) {
                        if (!chosen) {
                            chosen = pv;
                            return;
                        }
                        if (*chosen == pv)
                            return;
                        const LabelDef* ldef = findLabelDef(lid);
                        std::string label_name = ldef ? ldef->name : std::to_string(lid);
                        if (std::find(value_labels.begin(), value_labels.end(), label_name) == value_labels.end())
                            value_labels.push_back(label_name);
                        std::string labels;
                        for (const auto& n : value_labels) {
                            if (!labels.empty())
                                labels += ", ";
                            labels += n;
                        }
                        throw std::runtime_error("AmbiguousProperty: property '" + ref.property_name +
                                                 "' has conflicting values in labels (" + labels +
                                                 "); use n::<Label>." + ref.property_name + " to select a label scope");
                    };
                    for (const auto& candidate : ref.candidates) {
                        auto lit = props.find(candidate.label_id);
                        if (lit == props.end() || candidate.prop_id >= lit->second.size() ||
                            !lit->second[candidate.prop_id].has_value())
                            continue;
                        const LabelDef* ldef = findLabelDef(candidate.label_id);
                        if (ldef &&
                            std::find(value_labels.begin(), value_labels.end(), ldef->name) == value_labels.end())
                            value_labels.push_back(ldef->name);
                        consider(*lit->second[candidate.prop_id], candidate.label_id);
                    }
                    if (!chosen) {
                        for (const auto& [lid, props_vec] : props) {
                            const LabelDef* ldef = findLabelDef(lid);
                            if (!ldef || ldef->name != kAnonLabelName)
                                continue;
                            for (const auto& pd : ldef->properties) {
                                if (pd.name == ref.property_name && pd.id < props_vec.size() &&
                                    props_vec[pd.id].has_value()) {
                                    consider(*props_vec[pd.id], lid);
                                    break;
                                }
                            }
                            if (chosen)
                                break;
                        }
                    }
                    if (chosen)
                        r = pvToValue(*chosen);
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

            auto read_props = [&](LabelId label_id, const Properties& props_vec) {
                const uint16_t prop_id = propIdFor(label_id);
                if (prop_id != kMissingProp && prop_id < props_vec.size()) {
                    const auto& pv = props_vec[prop_id];
                    if (pv.has_value())
                        r = pvToValue(*pv);
                }
            };
            for (const auto& [label_id, props_vec] : *props)
                read_props(label_id, props_vec);
            if (anon_props)
                read_props(anon_lid, *anon_props);

            // Fallback: DDL may have created the property mid-query after the
            // static LabelDef snapshot was taken. Reload from meta to preserve
            // dynamic-property visibility for freshly created labels/props.
            if (std::holds_alternative<std::monostate>(r) && eval_ctx_.meta) {
                auto read_fallback = [&](LabelId label_id, const Properties& props_vec) {
                    if (!std::holds_alternative<std::monostate>(r) || props_vec.empty())
                        return;
                    const auto* fresh_ldef = findLabelDef(label_id);
                    if (!fresh_ldef)
                        return;
                    for (const auto& pd : fresh_ldef->properties) {
                        if (pd.name == ref.property && pd.id < props_vec.size() && props_vec[pd.id].has_value()) {
                            r = pvToValue(*props_vec[pd.id]);
                            break;
                        }
                    }
                };
                for (const auto& [label_id, props_vec] : *props)
                    read_fallback(label_id, props_vec);
                if (anon_props)
                    read_fallback(anon_lid, *anon_props);
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
