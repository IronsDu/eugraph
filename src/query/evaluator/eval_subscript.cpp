#include "query/evaluator/vectorized_evaluator.hpp"

#include "query/catalog/catalog.hpp"
#include "query/planner/bound_expression/bound_subscript.hpp"

namespace eugraph {
namespace compute {

namespace {

Value pvToValue(const PropertyValue& pv) {
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
}

} // namespace

void VectorizedEvaluator::evalSubscript(const binder::BoundSubscript& sub, const DataChunk& input, Column& result,
                                        size_t count) {
    auto list_eval = evaluateInternal(sub.list, input);
    auto idx_eval = evaluateInternal(sub.index, input);
    if (!list_eval.column || !idx_eval.column)
        return;

    for (size_t i = 0; i < count; ++i) {
        if (list_eval.column->isNull(i) || idx_eval.column->isNull(i)) {
            result.setNull(i);
            continue;
        }
        Value list_val = list_eval.column->getValue(i);
        Value idx_val = idx_eval.column->getValue(i);

        if (std::holds_alternative<ListValue>(list_val) && std::holds_alternative<int64_t>(idx_val)) {
            const auto& lv = std::get<ListValue>(list_val);
            int64_t idx = std::get<int64_t>(idx_val);
            if (idx < 0)
                idx += static_cast<int64_t>(lv.elements.size());
            if (idx >= 0 && static_cast<size_t>(idx) < lv.elements.size()) {
                result.setValue(i, lv.elements[static_cast<size_t>(idx)].value);
            } else {
                result.setNull(i);
            }
        } else if (std::holds_alternative<MapValue>(list_val) && std::holds_alternative<std::string>(idx_val)) {
            const auto& mv = std::get<MapValue>(list_val);
            const auto& key = std::get<std::string>(idx_val);
            bool found = false;
            for (const auto& [k, v] : mv.entries) {
                if (k == key) {
                    result.setValue(i, v.value);
                    found = true;
                    break;
                }
            }
            if (!found)
                result.setNull(i);
        } else if (std::holds_alternative<VertexValue>(list_val) && std::holds_alternative<std::string>(idx_val)) {
            // Dynamic property access on node: n['name']
            const auto& vertex = std::get<VertexValue>(list_val);
            const auto& key = std::get<std::string>(idx_val);
            Value r;
            for (const auto& [label_id, props_vec] : vertex.properties) {
                const LabelDef* ldef = nullptr;
                if (eval_ctx_.label_defs) {
                    auto it = eval_ctx_.label_defs->find(label_id);
                    if (it != eval_ctx_.label_defs->end())
                        ldef = &it->second;
                }
                if (!ldef && eval_ctx_.catalog)
                    ldef = eval_ctx_.catalog->lookupLabel(label_id);
                if (!ldef)
                    continue;
                for (const auto& pd : ldef->properties) {
                    if (pd.name == key && pd.id < props_vec.size()) {
                        const auto& pv = props_vec[pd.id];
                        if (pv.has_value())
                            r = pvToValue(*pv);
                        goto vprop_found;
                    }
                }
            }
        vprop_found:
            result.setValue(i, r);
        } else if (std::holds_alternative<EdgeValue>(list_val) && std::holds_alternative<std::string>(idx_val)) {
            // Dynamic property access on edge: r['name']
            const auto& edge = std::get<EdgeValue>(list_val);
            const auto& key = std::get<std::string>(idx_val);
            Value r;
            if (edge.properties.has_value() && eval_ctx_.catalog) {
                const auto* eldef = eval_ctx_.catalog->lookupEdgeLabel(edge.label_id);
                if (eldef) {
                    const auto& props = *edge.properties;
                    for (const auto& pd : eldef->properties) {
                        if (pd.name == key && pd.id < props.size()) {
                            const auto& pv = props[pd.id];
                            if (pv.has_value())
                                r = pvToValue(*pv);
                            break;
                        }
                    }
                }
            }
            result.setValue(i, r);
        } else if (std::holds_alternative<MapValue>(list_val)) {
            throw std::runtime_error("TypeError: MapElementAccessByNonString");
        } else {
            throw std::runtime_error("TypeError: InvalidArgumentType");
        }
    }
}

} // namespace compute
} // namespace eugraph
