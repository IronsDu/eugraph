#pragma once

#include "common/types/graph_types.hpp"
#include "compute_service/executor/row.hpp"

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

} // namespace scalar
} // namespace function
} // namespace eugraph
