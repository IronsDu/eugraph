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
    if (!std::holds_alternative<VertexValue>(arg))
        return Value{};
    const auto& vv = std::get<VertexValue>(arg);
    if (!vv.labels || !ctx.catalog)
        return Value{ListValue{}};
    ListValue lv;
    for (LabelId lid : *vv.labels) {
        auto* def = ctx.catalog->lookupLabel(lid);
        if (def) {
            lv.elements.push_back(ValueStorage{Value(def->name)});
        }
    }
    return Value(std::move(lv));
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
        for (size_t pid = 0; pid < props.size(); ++pid) {
            if (!props[pid].has_value())
                continue;
            if (pid < label_def->properties.size()) {
                lv.elements.push_back(ValueStorage{Value(label_def->properties[pid].name)});
            }
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
    for (size_t pid = 0; pid < props.size(); ++pid) {
        if (!props[pid].has_value())
            continue;
        if (pid < edge_def->properties.size()) {
            lv.elements.push_back(ValueStorage{Value(edge_def->properties[pid].name)});
        }
    }
    return Value(std::move(lv));
}

// Unified keys scalar/batch (dispatches by runtime type)

inline Value keysScalarFn(const std::vector<Value>& args, const EvalContext& ctx) {
    if (args.empty())
        return Value{};
    const auto& v = args[0];
    if (std::holds_alternative<VertexValue>(v))
        return keysVertexImpl(v, ctx);
    if (std::holds_alternative<EdgeValue>(v))
        return keysEdgeImpl(v, ctx);
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

} // namespace scalar
} // namespace function
} // namespace eugraph
