#pragma once

#include "query/executor/data_chunk.hpp"
#include "query/executor/row.hpp"
#include "query/function/function_def.hpp"

namespace eugraph {
namespace function {
namespace scalar {

/// last(List<Any>) -> Any: returns the last element of a list, or null if empty.
inline Value lastImpl(const Value& arg) {
    if (!std::holds_alternative<ListValue>(arg))
        return Value{};
    const auto& lv = std::get<ListValue>(arg);
    if (lv.elements.empty())
        return Value{};
    return lv.elements.back().value;
}

inline Value lastScalarFn(const std::vector<Value>& args, const EvalContext& /*ctx*/) {
    if (args.empty())
        return Value{};
    return lastImpl(args[0]);
}

inline void lastBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                        const EvalContext& /*ctx*/) {
    if (args.empty())
        return;
    const auto& arg_col = *args[0];
    for (size_t i = 0; i < count; ++i) {
        result.setValue(i, lastImpl(arg_col.getValue(i)));
    }
}

} // namespace scalar
} // namespace function
} // namespace eugraph
