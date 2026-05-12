#pragma once

#include "common/types/graph_types.hpp"
#include "query/executor/data_chunk.hpp"
#include "query/executor/row.hpp"

namespace eugraph {
namespace function {
namespace scalar {

/// id(vertex) -> vertex.id as int64
/// id(edge)   -> edge.id as int64
inline Value idImpl(const Value& arg) {
    if (std::holds_alternative<VertexValue>(arg)) {
        return Value(static_cast<int64_t>(std::get<VertexValue>(arg).id));
    }
    if (std::holds_alternative<EdgeValue>(arg)) {
        return Value(static_cast<int64_t>(std::get<EdgeValue>(arg).id));
    }
    return Value{};
}

/// Unified scalar callback for FunctionRegistry.
inline Value idScalarFn(const std::vector<Value>& args) {
    if (args.empty())
        return Value{};
    return idImpl(args[0]);
}

/// Batch scalar callback: extracts id for all rows at once.
inline void idBatchFn(const std::vector<const Column*>& args, Column& result, size_t count) {
    if (args.empty())
        return;
    const auto& arg_col = *args[0];
    for (size_t i = 0; i < count; ++i) {
        result.setValue(i, idImpl(arg_col.getValue(i)));
    }
}

} // namespace scalar
} // namespace function
} // namespace eugraph
