#pragma once

#include "query/dataset/data_chunk.hpp"
#include "query/dataset/row.hpp"
#include "query/function/function_def.hpp"

namespace eugraph {
namespace function {
namespace scalar {

inline Value startNodeImpl(const Value& arg) {
    if (std::holds_alternative<EdgeValuePtr>(arg)) {
        const auto& ev = (*std::get<EdgeValuePtr>(arg));
        VertexValue vv;
        vv.id = ev.src_id;
        return Value(mk<VertexValue>(std::move(vv)));
    }
    if (std::holds_alternative<std::monostate>(arg))
        return Value{};
    throw std::runtime_error("TypeError: InvalidArgumentValue");
}

inline Value endNodeImpl(const Value& arg) {
    if (std::holds_alternative<EdgeValuePtr>(arg)) {
        const auto& ev = (*std::get<EdgeValuePtr>(arg));
        VertexValue vv;
        vv.id = ev.dst_id;
        return Value(mk<VertexValue>(std::move(vv)));
    }
    if (std::holds_alternative<std::monostate>(arg))
        return Value{};
    throw std::runtime_error("TypeError: InvalidArgumentValue");
}

inline void startNodeBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                             const EvalContext& /*ctx*/) {
    if (args.empty())
        return;
    const auto& arg_col = *args[0];
    // A CONSTANT edge column broadcasts one edge to every row (this is how
    // PatternComprehensionApply feeds the correlated path relationship into the
    // per-apply subplan). Synthesize the endpoint once and share the handle:
    // complex-14 evaluated startNode(r) once per row for thousands of rows over
    // the same 12 relationships.
    if (arg_col.form == VectorForm::CONSTANT) {
        Value endpoint = startNodeImpl(arg_col.constant_value);
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, endpoint);
        return;
    }
    for (size_t i = 0; i < count; ++i)
        result.setValue(i, startNodeImpl(arg_col.getValue(i)));
}

inline void endNodeBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                           const EvalContext& /*ctx*/) {
    if (args.empty())
        return;
    const auto& arg_col = *args[0];
    if (arg_col.form == VectorForm::CONSTANT) {
        Value endpoint = endNodeImpl(arg_col.constant_value);
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, endpoint);
        return;
    }
    for (size_t i = 0; i < count; ++i)
        result.setValue(i, endNodeImpl(arg_col.getValue(i)));
}

} // namespace scalar
} // namespace function
} // namespace eugraph
