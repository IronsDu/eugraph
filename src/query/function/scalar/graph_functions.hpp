#pragma once

#include "common/types/property_home.hpp"
#include "query/catalog/catalog.hpp"
#include "query/dataset/data_chunk.hpp"
#include "query/dataset/row.hpp"

#include "query/function/function_def.hpp"
#include <algorithm>

namespace eugraph {
namespace function {
namespace scalar {

// --- labels ---

inline Value labelsImpl(const Value& arg, const EvalContext& ctx) {
    if (std::holds_alternative<VertexValuePtr>(arg)) {
        const auto& vv = (*std::get<VertexValuePtr>(arg));
        if (vv.deleted)
            throw std::runtime_error("EntityNotFound: DeletedEntityAccess");

        if (!vv.labels || !ctx.catalog)
            return Value(mk<ListValue>());
        ListValue lv;
        LabelId anon_id = ctx.catalog->getAnonLabelId();
        for (LabelId lid : *vv.labels) {
            if (lid == anon_id)
                continue;
            auto* def = ctx.catalog->lookupLabel(lid);
            if (def) {
                lv.elements.push_back(ValueStorage{Value(def->name)});
            }
        }
        return Value(mk<ListValue>(std::move(lv)));
    }
    if (std::holds_alternative<EdgeValuePtr>(arg)) {
        const auto& ev = (*std::get<EdgeValuePtr>(arg));
        if (!ctx.catalog)
            return Value(mk<ListValue>());
        auto* def = ctx.catalog->lookupEdgeLabel(ev.label_id);
        if (!def)
            return Value(mk<ListValue>());
        // labels(r) on a relationship returns [type(r)]
        return Value(mk<ListValue>(std::vector<ValueStorage>{ValueStorage{Value(def->name)}}));
    }
    if (std::holds_alternative<std::monostate>(arg))
        return Value{};
    throw std::runtime_error("TypeError: InvalidArgumentValue");
}

inline void labelsBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                          const EvalContext& ctx) {
    if (args.empty())
        return;
    const auto& arg_col = *args[0];
    for (size_t i = 0; i < count; ++i) {
        result.setValue(i, labelsImpl(arg_col.getValue(i), ctx));
    }
}

/// Labels the vertex actually holds (falls back to materialized labels when
/// the label set was not requested).
inline LabelIdSet heldLabelsOf(const VertexValue& vv) {
    if (vv.labels.has_value())
        return *vv.labels;
    LabelIdSet held;
    for (const auto& [lid, _] : vv.properties)
        held.insert(lid);
    return held;
}

/// Flatten a vertex's per-label properties into a name -> value map.
/// Ambiguity (two homes for the same key, regardless of the values) is an
/// error: a Cypher MAP cannot hold two entries with the same key, and the
/// weak view already refuses to pick a scope.
template <typename Lookup>
inline std::vector<std::pair<std::string, PropertyValue>>
flattenVertexProperties(const VertexValue& vv, Lookup&& lookup, LabelId anon_label_id) {
    LabelIdSet held = heldLabelsOf(vv);
    std::vector<std::string> names;
    for (LabelId lid : held) {
        const LabelDef* def = lookup(lid);
        if (!def)
            continue;
        for (const auto& pd : def->properties) {
            if (std::find(names.begin(), names.end(), pd.name) == names.end())
                names.push_back(pd.name);
        }
    }
    std::vector<std::pair<std::string, PropertyValue>> out;
    for (const auto& name : names) {
        auto home = resolvePropertyHome(lookup, held, vv.properties, name, anon_label_id);
        if (home.count > 1) {
            throw std::runtime_error("AmbiguousProperty: property '" + name +
                                     "' exists in multiple places on this vertex (" + joinHomeNames(home.names) +
                                     "); use n::<Label>." + name + " to select one");
        }
        if (home.count == 1 && home.value.has_value())
            out.emplace_back(name, *home.value);
    }
    return out;
}

// --- keys(Vertex) ---

inline Value keysVertexImpl(const Value& arg, const EvalContext& ctx) {
    if (!std::holds_alternative<VertexValuePtr>(arg))
        return Value{};
    const auto& vv = (*std::get<VertexValuePtr>(arg));
    if (!ctx.catalog)
        return Value(mk<ListValue>());
    ListValue lv;
    std::vector<std::string> names;
    for (const auto& [lid, props] : vv.properties) {
        auto* label_def = ctx.catalog->lookupLabel(lid);
        if (!label_def)
            continue;
        for (const auto& pd : label_def->properties) {
            if (pd.id < props.size() && props[pd.id].has_value() &&
                std::find(names.begin(), names.end(), pd.name) == names.end()) {
                names.push_back(pd.name);
                lv.elements.push_back(ValueStorage{Value(pd.name)});
            }
        }
    }
    return Value(mk<ListValue>(std::move(lv)));
}

// --- keys(Edge) ---

inline Value keysEdgeImpl(const Value& arg, const EvalContext& ctx) {
    if (!std::holds_alternative<EdgeValuePtr>(arg))
        return Value{};
    const auto& ev = (*std::get<EdgeValuePtr>(arg));
    if (!ev.properties || !ctx.catalog)
        return Value(mk<ListValue>());
    auto* edge_def = ctx.catalog->lookupEdgeLabel(ev.label_id);
    if (!edge_def)
        return Value(mk<ListValue>());
    const auto& props = *ev.properties;
    ListValue lv;
    for (const auto& pd : edge_def->properties) {
        if (pd.id < props.size() && props[pd.id].has_value())
            lv.elements.push_back(ValueStorage{Value(pd.name)});
    }
    return Value(mk<ListValue>(std::move(lv)));
}

// Unified keys scalar/batch (dispatches by runtime type)

// Forward declarations for unified dispatch
inline Value keysMapImpl(const Value& arg);

inline Value keysScalarFn(const std::vector<Value>& args, const EvalContext& ctx) {
    if (args.empty())
        return Value{};
    const auto& v = args[0];
    if (std::holds_alternative<VertexValuePtr>(v))
        return keysVertexImpl(v, ctx);
    if (std::holds_alternative<EdgeValuePtr>(v))
        return keysEdgeImpl(v, ctx);
    if (std::holds_alternative<MapValuePtr>(v))
        return keysMapImpl(v);
    return Value{};
}

inline void keysBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext& ctx) {
    if (args.empty())
        return;
    const auto& arg_col = *args[0];
    for (size_t i = 0; i < count; ++i) {
        result.setValue(i, keysScalarFn({arg_col.getValue(i)}, ctx));
    }
}

// --- keys(Map) ---

inline Value keysMapImpl(const Value& arg) {
    if (!std::holds_alternative<MapValuePtr>(arg))
        return Value{};
    const auto& mv = (*std::get<MapValuePtr>(arg));
    ListValue lv;
    for (const auto& [key, val] : mv.entries) {
        lv.elements.push_back(ValueStorage{Value(key)});
    }
    return Value(mk<ListValue>(std::move(lv)));
}

// --- properties(Vertex) ---

inline Value propertyValueToRuntimeValue(const PropertyValue& pv) {
    return std::visit(
        [](const auto& v) -> Value {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return Value{};
            } else if constexpr (std::is_same_v<T, bool>) {
                return Value(v);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                return Value(v);
            } else if constexpr (std::is_same_v<T, double>) {
                return Value(v);
            } else if constexpr (std::is_same_v<T, std::string>) {
                return Value(v);
            } else if constexpr (std::is_same_v<T, std::vector<int64_t>>) {
                ListValue lv;
                for (auto elem : v)
                    lv.elements.push_back(ValueStorage{Value(elem)});
                return Value(mk<ListValue>(std::move(lv)));
            } else if constexpr (std::is_same_v<T, std::vector<double>>) {
                ListValue lv;
                for (auto elem : v)
                    lv.elements.push_back(ValueStorage{Value(elem)});
                return Value(mk<ListValue>(std::move(lv)));
            } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                ListValue lv;
                for (auto elem : v)
                    lv.elements.push_back(ValueStorage{Value(std::move(elem))});
                return Value(mk<ListValue>(std::move(lv)));
            } else if constexpr (std::is_same_v<T, DateTimeValue>) {
                return Value(v);
            } else if constexpr (std::is_same_v<T, TimeValue>) {
                return Value(v);
            } else if constexpr (std::is_same_v<T, DurationValue>) {
                return Value(v);
            } else if constexpr (std::is_same_v<T, std::vector<DateTimeValue>>) {
                ListValue lv;
                for (const auto& elem : v)
                    lv.elements.push_back(ValueStorage{Value(elem)});
                return Value(mk<ListValue>(std::move(lv)));
            } else if constexpr (std::is_same_v<T, std::vector<TimeValue>>) {
                ListValue lv;
                for (const auto& elem : v)
                    lv.elements.push_back(ValueStorage{Value(elem)});
                return Value(mk<ListValue>(std::move(lv)));
            } else if constexpr (std::is_same_v<T, std::vector<DurationValue>>) {
                ListValue lv;
                for (const auto& elem : v)
                    lv.elements.push_back(ValueStorage{Value(elem)});
                return Value(mk<ListValue>(std::move(lv)));
            } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
                return Value(mk<BytesValue>(v));
            }
        },
        pv);
}

inline Value propertiesVertexImpl(const Value& arg, const EvalContext& ctx) {
    if (!std::holds_alternative<VertexValuePtr>(arg))
        return Value{};
    const auto& vv = (*std::get<VertexValuePtr>(arg));
    if (!ctx.catalog)
        return Value(mk<MapValue>());
    MapValue mv;
    LabelId anon_id = ctx.catalog->getAnonLabelId();
    auto flattened = flattenVertexProperties(vv, [&](LabelId lid) { return ctx.catalog->lookupLabel(lid); }, anon_id);
    for (auto& [name, pv] : flattened)
        mv.entries.push_back({std::move(name), ValueStorage{propertyValueToRuntimeValue(pv)}});
    return Value(mk<MapValue>(std::move(mv)));
}

inline Value propertiesEdgeImpl(const Value& arg, const EvalContext& ctx) {
    if (!std::holds_alternative<EdgeValuePtr>(arg))
        return Value{};
    const auto& ev = (*std::get<EdgeValuePtr>(arg));
    if (!ev.properties || !ctx.catalog)
        return Value(mk<MapValue>());
    auto* edge_def = ctx.catalog->lookupEdgeLabel(ev.label_id);
    if (!edge_def)
        return Value(mk<MapValue>());
    MapValue mv;
    const auto& props = *ev.properties;
    for (const auto& pd : edge_def->properties) {
        if (pd.id < props.size() && props[pd.id].has_value()) {
            Value val = propertyValueToRuntimeValue(*props[pd.id]);
            mv.entries.push_back({pd.name, ValueStorage{std::move(val)}});
        }
    }
    return Value(mk<MapValue>(std::move(mv)));
}

// Unified properties scalar/batch (dispatches by runtime type)

inline Value propertiesScalarFn(const std::vector<Value>& args, const EvalContext& ctx) {
    if (args.empty())
        return Value{};
    const auto& v = args[0];
    if (std::holds_alternative<VertexValuePtr>(v)) {
        const auto& vv = (*std::get<VertexValuePtr>(v));
        if (vv.id == INVALID_VERTEX_ID)
            return Value{};
        return propertiesVertexImpl(v, ctx);
    }
    if (std::holds_alternative<EdgeValuePtr>(v)) {
        const auto& ev = (*std::get<EdgeValuePtr>(v));
        if (ev.id == INVALID_EDGE_ID)
            return Value{};
        return propertiesEdgeImpl(v, ctx);
    }
    if (std::holds_alternative<MapValuePtr>(v))
        return v;
    return Value{};
}

inline void propertiesBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                              const EvalContext& ctx) {
    if (args.empty())
        return;
    const auto& arg_col = *args[0];
    for (size_t i = 0; i < count; ++i) {
        result.setValue(i, propertiesScalarFn({arg_col.getValue(i)}, ctx));
    }
}

} // namespace scalar
} // namespace function
} // namespace eugraph
