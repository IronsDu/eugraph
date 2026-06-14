#pragma once

#include "query/catalog/catalog.hpp"
#include "query/dataset/data_chunk.hpp"
#include "query/dataset/row.hpp"
#include "query/function/function_def.hpp"

namespace eugraph {
namespace function {
namespace scalar {

// --- labels ---

inline Value labelsImpl(const Value& arg, const EvalContext& ctx) {
    if (std::holds_alternative<VertexValue>(arg)) {
        const auto& vv = std::get<VertexValue>(arg);
        if (!vv.labels || !ctx.catalog)
            return Value{ListValue{}};
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
        return Value(std::move(lv));
    }
    if (std::holds_alternative<std::monostate>(arg))
        return Value{};
    throw std::runtime_error("TypeError: InvalidArgumentValue");
}

inline Value labelsScalarFn(const std::vector<Value>& args, const EvalContext& ctx) {
    if (args.empty())
        return Value{};
    return labelsImpl(args[0], ctx);
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

// --- keys(Vertex) ---

inline Value keysVertexImpl(const Value& arg, const EvalContext& ctx) {
    if (!std::holds_alternative<VertexValue>(arg))
        return Value{};
    const auto& vv = std::get<VertexValue>(arg);
    if (!ctx.catalog)
        return Value{ListValue{}};
    ListValue lv;
    for (const auto& [lid, props] : vv.properties) {
        auto* label_def = ctx.catalog->lookupLabel(lid);
        if (!label_def)
            continue;
        for (const auto& pd : label_def->properties) {
            if (pd.id < props.size() && props[pd.id].has_value())
                lv.elements.push_back(ValueStorage{Value(pd.name)});
        }
    }
    return Value(std::move(lv));
}

// --- keys(Edge) ---

inline Value keysEdgeImpl(const Value& arg, const EvalContext& ctx) {
    if (!std::holds_alternative<EdgeValue>(arg))
        return Value{};
    const auto& ev = std::get<EdgeValue>(arg);
    if (!ev.properties || !ctx.catalog)
        return Value{ListValue{}};
    auto* edge_def = ctx.catalog->lookupEdgeLabel(ev.label_id);
    if (!edge_def)
        return Value{ListValue{}};
    const auto& props = *ev.properties;
    ListValue lv;
    for (const auto& pd : edge_def->properties) {
        if (pd.id < props.size() && props[pd.id].has_value())
            lv.elements.push_back(ValueStorage{Value(pd.name)});
    }
    return Value(std::move(lv));
}

// Unified keys scalar/batch (dispatches by runtime type)

// Forward declarations for unified dispatch
inline Value keysMapImpl(const Value& arg);

inline Value keysScalarFn(const std::vector<Value>& args, const EvalContext& ctx) {
    if (args.empty())
        return Value{};
    const auto& v = args[0];
    if (std::holds_alternative<VertexValue>(v))
        return keysVertexImpl(v, ctx);
    if (std::holds_alternative<EdgeValue>(v))
        return keysEdgeImpl(v, ctx);
    if (std::holds_alternative<MapValue>(v))
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
    if (!std::holds_alternative<MapValue>(arg))
        return Value{};
    const auto& mv = std::get<MapValue>(arg);
    ListValue lv;
    for (const auto& [key, val] : mv.entries) {
        lv.elements.push_back(ValueStorage{Value(key)});
    }
    return Value(std::move(lv));
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
                return Value(std::move(lv));
            } else if constexpr (std::is_same_v<T, std::vector<double>>) {
                ListValue lv;
                for (auto elem : v)
                    lv.elements.push_back(ValueStorage{Value(elem)});
                return Value(std::move(lv));
            } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                ListValue lv;
                for (auto elem : v)
                    lv.elements.push_back(ValueStorage{Value(std::move(elem))});
                return Value(std::move(lv));
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
                return Value(std::move(lv));
            } else if constexpr (std::is_same_v<T, std::vector<TimeValue>>) {
                ListValue lv;
                for (const auto& elem : v)
                    lv.elements.push_back(ValueStorage{Value(elem)});
                return Value(std::move(lv));
            } else if constexpr (std::is_same_v<T, std::vector<DurationValue>>) {
                ListValue lv;
                for (const auto& elem : v)
                    lv.elements.push_back(ValueStorage{Value(elem)});
                return Value(std::move(lv));
            }
        },
        pv);
}

inline Value propertiesVertexImpl(const Value& arg, const EvalContext& ctx) {
    if (!std::holds_alternative<VertexValue>(arg))
        return Value{};
    const auto& vv = std::get<VertexValue>(arg);
    if (!ctx.catalog)
        return Value{MapValue{}};
    MapValue mv;
    for (const auto& [lid, props] : vv.properties) {
        auto* label_def = ctx.catalog->lookupLabel(lid);
        if (!label_def)
            continue;
        for (const auto& pd : label_def->properties) {
            if (pd.id < props.size() && props[pd.id].has_value()) {
                Value val = propertyValueToRuntimeValue(*props[pd.id]);
                mv.entries.push_back({pd.name, ValueStorage{std::move(val)}});
            }
        }
    }
    return Value(std::move(mv));
}

inline Value propertiesEdgeImpl(const Value& arg, const EvalContext& ctx) {
    if (!std::holds_alternative<EdgeValue>(arg))
        return Value{};
    const auto& ev = std::get<EdgeValue>(arg);
    if (!ev.properties || !ctx.catalog)
        return Value{MapValue{}};
    auto* edge_def = ctx.catalog->lookupEdgeLabel(ev.label_id);
    if (!edge_def)
        return Value{MapValue{}};
    MapValue mv;
    const auto& props = *ev.properties;
    for (const auto& pd : edge_def->properties) {
        if (pd.id < props.size() && props[pd.id].has_value()) {
            Value val = propertyValueToRuntimeValue(*props[pd.id]);
            mv.entries.push_back({pd.name, ValueStorage{std::move(val)}});
        }
    }
    return Value(std::move(mv));
}

// Unified properties scalar/batch (dispatches by runtime type)

inline Value propertiesScalarFn(const std::vector<Value>& args, const EvalContext& ctx) {
    if (args.empty())
        return Value{};
    const auto& v = args[0];
    if (std::holds_alternative<VertexValue>(v)) {
        const auto& vv = std::get<VertexValue>(v);
        if (vv.id == INVALID_VERTEX_ID)
            return Value{};
        return propertiesVertexImpl(v, ctx);
    }
    if (std::holds_alternative<EdgeValue>(v)) {
        const auto& ev = std::get<EdgeValue>(v);
        if (ev.id == INVALID_EDGE_ID)
            return Value{};
        return propertiesEdgeImpl(v, ctx);
    }
    if (std::holds_alternative<MapValue>(v))
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
