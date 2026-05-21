#pragma once

#include "query/dataset/data_chunk.hpp"
#include "query/dataset/row.hpp"
#include "query/function/function_def.hpp"

namespace eugraph {
namespace function {
namespace scalar {

// --- last ---

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

// --- head ---

inline Value headImpl(const Value& arg) {
    if (!std::holds_alternative<ListValue>(arg))
        return Value{};
    const auto& lv = std::get<ListValue>(arg);
    if (lv.elements.empty())
        return Value{};
    return lv.elements.front().value;
}

inline Value headScalarFn(const std::vector<Value>& args, const EvalContext& /*ctx*/) {
    if (args.empty())
        return Value{};
    return headImpl(args[0]);
}

inline void headBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                        const EvalContext& /*ctx*/) {
    if (args.empty())
        return;
    const auto& arg_col = *args[0];
    for (size_t i = 0; i < count; ++i) {
        result.setValue(i, headImpl(arg_col.getValue(i)));
    }
}

// --- tail ---

inline Value tailImpl(const Value& arg) {
    if (!std::holds_alternative<ListValue>(arg))
        return Value{};
    const auto& lv = std::get<ListValue>(arg);
    if (lv.elements.size() <= 1)
        return Value{ListValue{}};
    ListValue result;
    result.elements.assign(lv.elements.begin() + 1, lv.elements.end());
    return Value{std::move(result)};
}

inline Value tailScalarFn(const std::vector<Value>& args, const EvalContext& /*ctx*/) {
    if (args.empty())
        return Value{};
    return tailImpl(args[0]);
}

inline void tailBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                        const EvalContext& /*ctx*/) {
    if (args.empty())
        return;
    const auto& arg_col = *args[0];
    for (size_t i = 0; i < count; ++i) {
        result.setValue(i, tailImpl(arg_col.getValue(i)));
    }
}

// --- reverse ---

inline Value reverseImpl(const Value& arg) {
    if (!std::holds_alternative<ListValue>(arg))
        return Value{};
    const auto& lv = std::get<ListValue>(arg);
    ListValue result;
    result.elements.assign(lv.elements.rbegin(), lv.elements.rend());
    return Value{std::move(result)};
}

inline Value reverseScalarFn(const std::vector<Value>& args, const EvalContext& /*ctx*/) {
    if (args.empty())
        return Value{};
    return reverseImpl(args[0]);
}

inline void reverseBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                           const EvalContext& /*ctx*/) {
    if (args.empty())
        return;
    const auto& arg_col = *args[0];
    for (size_t i = 0; i < count; ++i) {
        result.setValue(i, reverseImpl(arg_col.getValue(i)));
    }
}

// --- size ---

inline Value sizeListImpl(const Value& arg) {
    if (!std::holds_alternative<ListValue>(arg))
        return Value{int64_t(0)};
    return Value{static_cast<int64_t>(std::get<ListValue>(arg).elements.size())};
}

inline Value sizeStringImpl(const Value& arg) {
    if (!std::holds_alternative<std::string>(arg))
        return Value{int64_t(0)};
    return Value{static_cast<int64_t>(std::get<std::string>(arg).size())};
}

inline Value sizeScalarFn(const std::vector<Value>& args, const EvalContext& /*ctx*/) {
    if (args.empty())
        return Value{int64_t(0)};
    const auto& v = args[0];
    if (std::holds_alternative<ListValue>(v))
        return sizeListImpl(v);
    if (std::holds_alternative<std::string>(v))
        return sizeStringImpl(v);
    return Value{int64_t(0)};
}

inline void sizeListBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                            const EvalContext& /*ctx*/) {
    if (args.empty())
        return;
    const auto& arg_col = *args[0];
    for (size_t i = 0; i < count; ++i) {
        result.setValue(i, sizeListImpl(arg_col.getValue(i)));
    }
}

inline void sizeStringBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                              const EvalContext& /*ctx*/) {
    if (args.empty())
        return;
    const auto& arg_col = *args[0];
    for (size_t i = 0; i < count; ++i) {
        result.setValue(i, sizeStringImpl(arg_col.getValue(i)));
    }
}

// --- range ---

inline Value rangeImpl(int64_t start, int64_t end, int64_t step) {
    if (step == 0)
        return Value{};
    ListValue lv;
    if (step > 0) {
        for (int64_t i = start; i <= end; i += step) {
            lv.elements.push_back(ValueStorage{Value(i)});
        }
    } else {
        for (int64_t i = start; i >= end; i += step) {
            lv.elements.push_back(ValueStorage{Value(i)});
        }
    }
    return Value(std::move(lv));
}

inline Value rangeScalarFn(const std::vector<Value>& args, const EvalContext& /*ctx*/) {
    if (args.size() < 2)
        return Value{};
    if (isNull(args[0]) || isNull(args[1]))
        return Value{};
    auto start = std::get<int64_t>(args[0]);
    auto end = std::get<int64_t>(args[1]);
    int64_t step = 1;
    if (args.size() >= 3) {
        if (isNull(args[2]))
            return Value{};
        step = std::get<int64_t>(args[2]);
    }
    return rangeImpl(start, end, step);
}

inline void rangeBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                         const EvalContext& ctx) {
    if (args.size() < 2)
        return;
    for (size_t i = 0; i < count; ++i) {
        std::vector<Value> row_args;
        row_args.reserve(args.size());
        for (auto* col : args) {
            row_args.push_back(col->getValue(i));
        }
        result.setValue(i, rangeScalarFn(row_args, ctx));
    }
}

} // namespace scalar
} // namespace function
} // namespace eugraph
