#include "query/evaluator/vectorized_evaluator.hpp"

#include <spdlog/spdlog.h>

#include "query/catalog/catalog.hpp"
#include "query/planner/bound_expression/bound_dynamic_property_ref.hpp"

namespace eugraph {
namespace compute {

void VectorizedEvaluator::evalPropertyRef(const binder::BoundPropertyRef& ref, const DataChunk& input, Column& result,
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
        if (std::holds_alternative<std::vector<int64_t>>(pv)) {
            ListValue lv;
            for (auto v : std::get<std::vector<int64_t>>(pv))
                lv.elements.push_back(ValueStorage{Value(v)});
            return Value(std::move(lv));
        }
        if (std::holds_alternative<std::vector<double>>(pv)) {
            ListValue lv;
            for (auto v : std::get<std::vector<double>>(pv))
                lv.elements.push_back(ValueStorage{Value(v)});
            return Value(std::move(lv));
        }
        if (std::holds_alternative<std::vector<std::string>>(pv)) {
            ListValue lv;
            for (auto& s : std::get<std::vector<std::string>>(pv))
                lv.elements.push_back(ValueStorage{Value(std::move(s))});
            return Value(std::move(lv));
        }
        if (std::holds_alternative<std::vector<DateTimeValue>>(pv)) {
            ListValue lv;
            for (const auto& v : std::get<std::vector<DateTimeValue>>(pv))
                lv.elements.push_back(ValueStorage{Value(v)});
            return Value(std::move(lv));
        }
        if (std::holds_alternative<std::vector<TimeValue>>(pv)) {
            ListValue lv;
            for (const auto& v : std::get<std::vector<TimeValue>>(pv))
                lv.elements.push_back(ValueStorage{Value(v)});
            return Value(std::move(lv));
        }
        if (std::holds_alternative<std::vector<DurationValue>>(pv)) {
            ListValue lv;
            for (const auto& v : std::get<std::vector<DurationValue>>(pv))
                lv.elements.push_back(ValueStorage{Value(v)});
            return Value(std::move(lv));
        }
        return Value{};
    };

    for (size_t i = 0; i < count; ++i) {
        Value ov = obj.column->getValue(i);
        Value r;
        if (std::holds_alternative<VertexValue>(ov)) {
            const auto& vertex = std::get<VertexValue>(ov);
            std::vector<Value> found;
            for (const auto& candidate : ref.candidates) {
                auto lit = vertex.properties.find(candidate.label_id);
                if (lit != vertex.properties.end()) {
                    const auto& props = lit->second;
                    if (candidate.prop_id < props.size() && props[candidate.prop_id].has_value()) {
                        found.push_back(pvToValue(*props[candidate.prop_id]));
                    }
                }
            }
            spdlog::info("[evalPropRef] vid={} prop_name='{}' candidates={} found={} props_labels={}",
                         std::holds_alternative<VertexValue>(ov) ? std::get<VertexValue>(ov).id : -1, ref.property_name,
                         ref.candidates.size(), found.size(), vertex.properties.size());
            // Fallback: a property may be registered on __anon__ in the
            // catalog but physically stored under the vertex's concrete
            // label.  Search all loaded labels by name.
            if (found.empty() && !ref.property_name.empty()) {
                for (const auto& [lid, props_vec] : vertex.properties) {
                    const LabelDef* ldef = nullptr;
                    if (eval_ctx_.label_defs) {
                        auto it = eval_ctx_.label_defs->find(lid);
                        if (it != eval_ctx_.label_defs->end())
                            ldef = &it->second;
                    }
                    if (!ldef && eval_ctx_.catalog)
                        ldef = eval_ctx_.catalog->lookupLabel(lid);
                    if (!ldef)
                        continue;
                    for (const auto& pd : ldef->properties) {
                        if (pd.name == ref.property_name && pd.id < props_vec.size()) {
                            const auto& pv = props_vec[pd.id];
                            if (pv.has_value()) {
                                found.push_back(pvToValue(*pv));
                                break;
                            }
                        }
                    }
                    if (!found.empty())
                        break;
                }
            }
            if (found.size() == 1) {
                r = std::move(found[0]);
            } else if (found.size() > 1) {
                ListValue lv;
                for (auto& v : found)
                    lv.elements.push_back(ValueStorage{std::move(v)});
                r = Value(std::move(lv));
            }
        } else if (std::holds_alternative<EdgeValue>(ov)) {
            const auto& edge = std::get<EdgeValue>(ov);
            // Structural fields (r.id, r.src_id, r.dst_id, r.label_id)
            if (ref.candidates.empty() && !ref.property_name.empty()) {
                if (ref.property_name == "id") {
                    r = Value(static_cast<int64_t>(edge.id));
                } else if (ref.property_name == "src_id") {
                    r = Value(static_cast<int64_t>(edge.src_id));
                } else if (ref.property_name == "dst_id") {
                    r = Value(static_cast<int64_t>(edge.dst_id));
                } else if (ref.property_name == "label_id") {
                    r = Value(static_cast<int64_t>(edge.label_id));
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

void VectorizedEvaluator::evalDynamicPropertyRef(const binder::BoundDynamicPropertyRef& ref, const DataChunk& input,
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
        if (std::holds_alternative<std::vector<int64_t>>(pv)) {
            ListValue lv;
            for (auto v : std::get<std::vector<int64_t>>(pv))
                lv.elements.push_back(ValueStorage{Value(v)});
            return Value(std::move(lv));
        }
        if (std::holds_alternative<std::vector<double>>(pv)) {
            ListValue lv;
            for (auto v : std::get<std::vector<double>>(pv))
                lv.elements.push_back(ValueStorage{Value(v)});
            return Value(std::move(lv));
        }
        if (std::holds_alternative<std::vector<std::string>>(pv)) {
            ListValue lv;
            for (auto& s : std::get<std::vector<std::string>>(pv))
                lv.elements.push_back(ValueStorage{Value(std::move(s))});
            return Value(std::move(lv));
        }
        if (std::holds_alternative<std::vector<DateTimeValue>>(pv)) {
            ListValue lv;
            for (const auto& v : std::get<std::vector<DateTimeValue>>(pv))
                lv.elements.push_back(ValueStorage{Value(v)});
            return Value(std::move(lv));
        }
        if (std::holds_alternative<std::vector<TimeValue>>(pv)) {
            ListValue lv;
            for (const auto& v : std::get<std::vector<TimeValue>>(pv))
                lv.elements.push_back(ValueStorage{Value(v)});
            return Value(std::move(lv));
        }
        if (std::holds_alternative<std::vector<DurationValue>>(pv)) {
            ListValue lv;
            for (const auto& v : std::get<std::vector<DurationValue>>(pv))
                lv.elements.push_back(ValueStorage{Value(v)});
            return Value(std::move(lv));
        }
        return Value{};
    };

    for (size_t i = 0; i < count; ++i) {
        Value ov = obj.column->getValue(i);
        Value r;
        if (std::holds_alternative<VertexValue>(ov)) {
            const auto& vertex = std::get<VertexValue>(ov);
            for (const auto& [label_id, props_vec] : vertex.properties) {
                const LabelDef* ldef = nullptr;
                if (eval_ctx_.label_defs) {
                    auto it = eval_ctx_.label_defs->find(label_id);
                    if (it != eval_ctx_.label_defs->end())
                        ldef = &it->second;
                }
                if (!ldef)
                    ldef = eval_ctx_.catalog->lookupLabel(label_id);
                if (!ldef)
                    continue;
                for (const auto& pd : ldef->properties) {
                    if (pd.name == ref.property) {
                        if (pd.id < props_vec.size()) {
                            const auto& pv = props_vec[pd.id];
                            if (pv.has_value())
                                r = pvToValue(*pv);
                        }
                        goto found;
                    }
                }
            }
        found:;
        } else if (std::holds_alternative<EdgeValue>(ov)) {
            const auto& edge = std::get<EdgeValue>(ov);
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
        } else if (std::holds_alternative<MapValue>(ov)) {
            const auto& mv = std::get<MapValue>(ov);
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
