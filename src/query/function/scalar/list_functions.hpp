#pragma once

#include "query/dataset/data_chunk.hpp"
#include "query/dataset/row.hpp"
#include "query/function/function_def.hpp"
#include "query/function/scalar/support/typed_batch.hpp"

#include <algorithm>

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

struct TailOp {
    static ListValue apply(const ListValue& v) {
        ListValue out;
        if (v.elements.size() > 1)
            out.elements.assign(v.elements.begin() + 1, v.elements.end());
        return out;
    }
};
struct ReverseListOp {
    static ListValue apply(const ListValue& v) {
        ListValue out;
        out.elements.reserve(v.elements.size());
        for (auto it = v.elements.rbegin(); it != v.elements.rend(); ++it)
            out.elements.push_back(*it);
        return out;
    }
};
struct SizeListOp {
    static int64_t apply(const ListValue& v) {
        return static_cast<int64_t>(v.elements.size());
    }
};

inline void tailBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                        const EvalContext& /*ctx*/) {
    if (args.empty())
        return;
    const Column& in = *args[0];
    if (in.type == binder::BoundTypeKind::LIST)
        typedUnaryBatch<ListValue, ListValue, TailOp>(in, result, count);
    else
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, tailImpl(in.getValue(i)));
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

inline void reverseBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                           const EvalContext& /*ctx*/) {
    if (args.empty())
        return;
    const Column& in = *args[0];
    if (in.type == binder::BoundTypeKind::LIST)
        typedUnaryBatch<ListValue, ListValue, ReverseListOp>(in, result, count);
    else
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, reverseImpl(in.getValue(i)));
}

// --- reverse for strings ---

inline Value reverseStringImpl(const Value& arg) {
    if (isNull(arg))
        return Value{};
    if (!std::holds_alternative<std::string>(arg))
        return Value{};
    std::string s = std::get<std::string>(arg);
    std::reverse(s.begin(), s.end());
    return Value{std::move(s)};
}

inline void reverseStringBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                                 const EvalContext& /*ctx*/) {
    if (args.empty())
        return;
    const auto& arg_col = *args[0];
    for (size_t i = 0; i < count; ++i) {
        result.setValue(i, reverseStringImpl(arg_col.getValue(i)));
    }
}

// --- size ---

inline Value sizeListImpl(const Value& arg) {
    // Cypher: size(null) = null. Any non-list non-null is a type error -> null.
    if (isNull(arg))
        return Value{};
    if (!std::holds_alternative<ListValue>(arg))
        return Value{};
    return Value{static_cast<int64_t>(std::get<ListValue>(arg).elements.size())};
}

inline Value sizeStringImpl(const Value& arg) {
    if (!std::holds_alternative<std::string>(arg))
        return Value{int64_t(0)};
    return Value{static_cast<int64_t>(std::get<std::string>(arg).size())};
}

inline void sizeListBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                            const EvalContext& /*ctx*/) {
    if (args.empty())
        return;
    const Column& in = *args[0];
    if (in.type == binder::BoundTypeKind::LIST)
        typedUnaryBatch<ListValue, int64_t, SizeListOp>(in, result, count);
    else
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, sizeListImpl(in.getValue(i)));
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
    // Cypher: null arguments propagate to null result (no error).
    for (const auto& a : args) {
        if (isNull(a))
            return Value{};
    }
    // Type validation: every argument must be INT64. Booleans, floats,
    // strings, lists, maps are rejected at runtime as ArgumentError.
    for (size_t i = 0; i < args.size(); ++i) {
        if (!std::holds_alternative<int64_t>(args[i])) {
            throw std::runtime_error("ArgumentError: InvalidArgumentType: range() argument " + std::to_string(i + 1) +
                                     " must be INTEGER");
        }
    }
    int64_t start = std::get<int64_t>(args[0]);
    int64_t end = std::get<int64_t>(args[1]);
    int64_t step = 1;
    if (args.size() >= 3) {
        step = std::get<int64_t>(args[2]);
        if (step == 0) {
            throw std::runtime_error("ArgumentError: NumberOutOfRange: range() step must be non-zero");
        }
    }
    return rangeImpl(start, end, step);
}

inline void rangeBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext& ctx) {
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
