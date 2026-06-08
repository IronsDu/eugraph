#pragma once

#include "query/dataset/data_chunk.hpp"
#include "query/dataset/row.hpp"
#include "query/function/function_def.hpp"

#include <cmath>

namespace eugraph {
namespace function {
namespace scalar {

// --- abs ---

inline Value absImpl(const Value& arg) {
    if (isNull(arg))
        return Value{};
    if (std::holds_alternative<int64_t>(arg))
        return Value(std::abs(std::get<int64_t>(arg)));
    if (std::holds_alternative<double>(arg))
        return Value(std::abs(std::get<double>(arg)));
    return Value{};
}

inline Value absScalarFn(const std::vector<Value>& args, const EvalContext& /*ctx*/) {
    return absImpl(args[0]);
}

inline void absBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                       const EvalContext& /*ctx*/) {
    for (size_t i = 0; i < count; i++) {
        result.setValue(i, absImpl(args[0]->getValue(i)));
    }
}

// --- sqrt ---

inline Value sqrtImpl(const Value& arg) {
    if (isNull(arg))
        return Value{};
    if (std::holds_alternative<double>(arg))
        return Value(std::sqrt(std::get<double>(arg)));
    if (std::holds_alternative<int64_t>(arg))
        return Value(std::sqrt(static_cast<double>(std::get<int64_t>(arg))));
    return Value{};
}

inline Value sqrtScalarFn(const std::vector<Value>& args, const EvalContext& /*ctx*/) {
    return sqrtImpl(args[0]);
}

inline void sqrtBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                        const EvalContext& /*ctx*/) {
    for (size_t i = 0; i < count; i++) {
        result.setValue(i, sqrtImpl(args[0]->getValue(i)));
    }
}

} // namespace scalar
} // namespace function
} // namespace eugraph
