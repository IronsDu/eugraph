#pragma once

#include "query/dataset/row.hpp"
#include "query/function/function_def.hpp"

namespace eugraph {
namespace function {
namespace scalar {

// --- coalesce ---

inline Value coalesceScalarFn(const std::vector<Value>& args, const EvalContext&) {
    for (const auto& arg : args) {
        if (!isNull(arg))
            return arg;
    }
    return Value{};
}

inline void coalesceBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext&) {
    for (size_t i = 0; i < count; ++i) {
        Value found{};
        for (auto* col : args) {
            auto v = col->getValue(i);
            if (!isNull(v)) {
                found = std::move(v);
                break;
            }
        }
        result.setValue(i, std::move(found));
    }
}

} // namespace scalar
} // namespace function
} // namespace eugraph
