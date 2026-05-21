#pragma once

#include "query/catalog/catalog.hpp"
#include "query/dataset/data_chunk.hpp"
#include "query/dataset/row.hpp"
#include "query/function/function_def.hpp"

namespace eugraph {
namespace function {
namespace scalar {

/// type(Edge) -> String: returns the edge type name.
inline Value typeImpl(const Value& arg, const EvalContext& ctx) {
    if (!std::holds_alternative<EdgeValue>(arg))
        return Value{};
    const auto& ev = std::get<EdgeValue>(arg);
    if (!ctx.catalog)
        return Value{};
    auto* def = ctx.catalog->lookupEdgeLabel(ev.label_id);
    if (!def)
        return Value{};
    return Value(def->name);
}

inline Value typeScalarFn(const std::vector<Value>& args, const EvalContext& ctx) {
    if (args.empty())
        return Value{};
    return typeImpl(args[0], ctx);
}

inline void typeBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext& ctx) {
    if (args.empty())
        return;
    const auto& arg_col = *args[0];
    for (size_t i = 0; i < count; ++i) {
        result.setValue(i, typeImpl(arg_col.getValue(i), ctx));
    }
}

} // namespace scalar
} // namespace function
} // namespace eugraph
