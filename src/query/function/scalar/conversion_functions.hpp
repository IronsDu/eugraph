#pragma once

#include "common/types/temporal_value.hpp"
#include "query/dataset/data_chunk.hpp"
#include "query/dataset/row.hpp"
#include "query/function/function_def.hpp"

namespace eugraph {
namespace function {
namespace scalar {

// --- toInteger ---

inline Value toIntegerImpl(const Value& arg) {
    if (isNull(arg))
        return Value{};
    if (std::holds_alternative<int64_t>(arg))
        return arg;
    if (std::holds_alternative<double>(arg))
        return Value(static_cast<int64_t>(std::get<double>(arg)));
    if (std::holds_alternative<bool>(arg))
        return Value(std::get<bool>(arg) ? int64_t(1) : int64_t(0));
    if (std::holds_alternative<std::string>(arg)) {
        try {
            const auto& s = std::get<std::string>(arg);
            size_t pos = 0;
            long long v = std::stoll(s, &pos);
            if (pos != s.size())
                return Value{};
            return Value(static_cast<int64_t>(v));
        } catch (...) {
            return Value{};
        }
    }
    return Value{};
}

inline Value toIntegerScalarFn(const std::vector<Value>& args, const EvalContext& /*ctx*/) {
    if (args.empty())
        return Value{};
    return toIntegerImpl(args[0]);
}

inline void toIntegerBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                             const EvalContext& /*ctx*/) {
    if (args.empty())
        return;
    const auto& arg_col = *args[0];
    for (size_t i = 0; i < count; ++i) {
        result.setValue(i, toIntegerImpl(arg_col.getValue(i)));
    }
}

// --- toFloat ---

inline Value toFloatImpl(const Value& arg) {
    if (isNull(arg))
        return Value{};
    if (std::holds_alternative<double>(arg))
        return arg;
    if (std::holds_alternative<int64_t>(arg))
        return Value(static_cast<double>(std::get<int64_t>(arg)));
    if (std::holds_alternative<std::string>(arg)) {
        try {
            const auto& s = std::get<std::string>(arg);
            size_t pos = 0;
            double v = std::stod(s, &pos);
            if (pos != s.size())
                return Value{};
            return Value(v);
        } catch (...) {
            return Value{};
        }
    }
    return Value{};
}

inline Value toFloatScalarFn(const std::vector<Value>& args, const EvalContext& /*ctx*/) {
    if (args.empty())
        return Value{};
    return toFloatImpl(args[0]);
}

inline void toFloatBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                           const EvalContext& /*ctx*/) {
    if (args.empty())
        return;
    const auto& arg_col = *args[0];
    for (size_t i = 0; i < count; ++i) {
        result.setValue(i, toFloatImpl(arg_col.getValue(i)));
    }
}

// --- toString ---

inline Value toStringImpl(const Value& arg) {
    if (isNull(arg))
        return Value{};
    if (std::holds_alternative<std::string>(arg))
        return arg;
    if (std::holds_alternative<int64_t>(arg))
        return Value(std::to_string(std::get<int64_t>(arg)));
    if (std::holds_alternative<double>(arg)) {
        std::string s = std::to_string(std::get<double>(arg));
        // Remove trailing zeros after decimal point
        auto dot = s.find('.');
        if (dot != std::string::npos) {
            auto last_non_zero = s.find_last_not_of('0');
            if (last_non_zero != std::string::npos && last_non_zero > dot) {
                s.erase(last_non_zero + 1);
            } else if (last_non_zero == dot) {
                s.erase(dot);
            }
        }
        return Value(std::move(s));
    }
    if (std::holds_alternative<bool>(arg))
        return Value(std::get<bool>(arg) ? std::string("true") : std::string("false"));
    if (std::holds_alternative<DateTimeValue>(arg))
        return Value(temporalToString(std::get<DateTimeValue>(arg)));
    if (std::holds_alternative<TimeValue>(arg))
        return Value(temporalToString(std::get<TimeValue>(arg)));
    if (std::holds_alternative<DurationValue>(arg))
        return Value(temporalToString(std::get<DurationValue>(arg)));
    return Value{};
}

inline Value toStringScalarFn(const std::vector<Value>& args, const EvalContext& /*ctx*/) {
    if (args.empty())
        return Value{};
    return toStringImpl(args[0]);
}

inline void toStringBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                            const EvalContext& /*ctx*/) {
    if (args.empty())
        return;
    const auto& arg_col = *args[0];
    for (size_t i = 0; i < count; ++i) {
        result.setValue(i, toStringImpl(arg_col.getValue(i)));
    }
}

// --- toBoolean ---

inline Value toBooleanImpl(const Value& arg) {
    if (isNull(arg))
        return Value{};
    if (std::holds_alternative<bool>(arg))
        return arg;
    if (std::holds_alternative<int64_t>(arg))
        return Value(std::get<int64_t>(arg) != 0);
    if (std::holds_alternative<double>(arg))
        return Value(std::get<double>(arg) != 0.0);
    if (std::holds_alternative<std::string>(arg)) {
        const auto& s = std::get<std::string>(arg);
        if (s == "true")
            return Value(true);
        if (s == "false")
            return Value(false);
        return Value{};
    }
    return Value{};
}

inline Value toBooleanScalarFn(const std::vector<Value>& args, const EvalContext& /*ctx*/) {
    if (args.empty())
        return Value{};
    return toBooleanImpl(args[0]);
}

inline void toBooleanBatchFn(const std::vector<const Column*>& args, Column& result, size_t count,
                             const EvalContext& /*ctx*/) {
    if (args.empty())
        return;
    const auto& arg_col = *args[0];
    for (size_t i = 0; i < count; ++i) {
        result.setValue(i, toBooleanImpl(arg_col.getValue(i)));
    }
}

} // namespace scalar
} // namespace function
} // namespace eugraph
