#pragma once

#include "query/dataset/data_chunk.hpp"
#include "query/dataset/row.hpp"
#include "query/function/function_def.hpp"

namespace eugraph {
namespace function {
namespace scalar {

inline Value startNodeImpl(const Value& arg) {
    if (std::holds_alternative<EdgeValue>(arg)) {
        const auto& ev = std::get<EdgeValue>(arg);
        VertexValue vv;
        vv.id = ev.src_id;
        return Value(std::move(vv));
    }
    if (std::holds_alternative<std::monostate>(arg))
        return Value{};
    throw std::runtime_error("TypeError: InvalidArgumentValue");
}

inline Value endNodeImpl(const Value& arg) {
    if (std::holds_alternative<EdgeValue>(arg)) {
        const auto& ev = std::get<EdgeValue>(arg);
        VertexValue vv;
        vv.id = ev.dst_id;
        return Value(std::move(vv));
    }
    if (std::holds_alternative<std::monostate>(arg))
        return Value{};
    throw std::runtime_error("TypeError: InvalidArgumentValue");
}

inline Value startNodeScalarFn(const std::vector<Value>& args, const EvalContext& /*ctx*/) {
    if (args.empty())
        return Value{};
    return startNodeImpl(args[0]);
}

inline void startNodeBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                             const EvalContext& /*ctx*/) {
    if (args.empty())
        return;
    const auto& arg_col = *args[0];
    for (size_t i = 0; i < count; ++i)
        result.setValue(i, startNodeImpl(arg_col.getValue(i)));
}

inline Value endNodeScalarFn(const std::vector<Value>& args, const EvalContext& /*ctx*/) {
    if (args.empty())
        return Value{};
    return endNodeImpl(args[0]);
}

inline void endNodeBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                           const EvalContext& /*ctx*/) {
    if (args.empty())
        return;
    const auto& arg_col = *args[0];
    for (size_t i = 0; i < count; ++i)
        result.setValue(i, endNodeImpl(arg_col.getValue(i)));
}

} // namespace scalar
} // namespace function
} // namespace eugraph
