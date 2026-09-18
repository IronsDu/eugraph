#pragma once

#include "common/types/query_error.hpp"
#include "common/types/temporal_value.hpp"
#include "query/dataset/data_chunk.hpp"
#include "query/dataset/row.hpp"
#include "query/function/function_def.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace eugraph {
namespace function {
namespace scalar {

// --- toInteger ---

/// double → int64：Java 的 `(long)` 语义（neo4j 的实现语言）。
/// NaN 归 0，超范围饱和到 INT64_MAX/INT64_MIN，而不是 C++ 的未定义行为。
inline int64_t doubleToInt64Clamped(double d) {
    if (std::isnan(d))
        return 0;
    if (d >= 9223372036854775808.0) // 2^63
        return std::numeric_limits<int64_t>::max();
    if (d <= -9223372036854775808.0)
        return std::numeric_limits<int64_t>::min();
    return static_cast<int64_t>(d);
}

inline Value toIntegerImpl(const Value& arg) {
    if (isNull(arg))
        return Value{};
    if (std::holds_alternative<int64_t>(arg))
        return arg;
    if (std::holds_alternative<double>(arg))
        return Value(doubleToInt64Clamped(std::get<double>(arg)));
    if (std::holds_alternative<bool>(arg))
        return Value(std::get<bool>(arg) ? int64_t(1) : int64_t(0));
    if (std::holds_alternative<std::string>(arg)) {
        const auto& s = std::get<std::string>(arg);
        if (s.empty())
            return Value{};
        // 整数串先按整数解析：这样越界能被精确判定（stod 会把大整数变成 double 后静默回绕）。
        // neo4j 对超范围字符串报 TypeError，对无法解析的字符串返回 NULL。
        size_t pos = 0;
        try {
            const int64_t v = std::stoll(s, &pos);
            if (pos == s.size())
                return Value(v);
        } catch (const std::out_of_range&) {
            throw QueryException(QueryErrorKind::Type, "integer, " + s + ", is too large");
        } catch (const std::invalid_argument&) {
            // 交给下面的浮点解析（"1.9" / "1e3" 等）
        }
        try {
            pos = 0;
            const double d = std::stod(s, &pos);
            if (pos != s.size())
                return Value{};
            if (d >= 9223372036854775808.0 || d <= -9223372036854775809.0)
                throw QueryException(QueryErrorKind::Type, "integer, " + s + ", is too large");
            return Value(static_cast<int64_t>(d));
        } catch (const QueryException&) {
            throw;
        } catch (...) {
            return Value{};
        }
    }
    if (std::holds_alternative<ListValue>(arg) || std::holds_alternative<MapValue>(arg) ||
        std::holds_alternative<VertexValue>(arg) || std::holds_alternative<EdgeValue>(arg) ||
        std::holds_alternative<PathValue>(arg))
        throw std::runtime_error("TypeError: InvalidArgumentValue");
    return Value{};
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
            if (s.empty())
                return Value{};
            size_t pos = 0;
            double v = std::stod(s, &pos);
            if (pos != s.size())
                return Value{};
            return Value(v);
        } catch (...) {
            return Value{};
        }
    }
    if (std::holds_alternative<bool>(arg) || std::holds_alternative<ListValue>(arg) ||
        std::holds_alternative<MapValue>(arg) || std::holds_alternative<VertexValue>(arg) ||
        std::holds_alternative<EdgeValue>(arg) || std::holds_alternative<PathValue>(arg))
        throw std::runtime_error("TypeError: InvalidArgumentValue");
    return Value{};
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
    if (std::holds_alternative<ListValue>(arg) || std::holds_alternative<MapValue>(arg) ||
        std::holds_alternative<VertexValue>(arg) || std::holds_alternative<EdgeValue>(arg) ||
        std::holds_alternative<PathValue>(arg))
        throw std::runtime_error("TypeError: InvalidArgumentValue");
    return Value{};
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
    if (std::holds_alternative<std::string>(arg)) {
        const auto& s = std::get<std::string>(arg);
        if (s == "true")
            return Value(true);
        if (s == "false")
            return Value(false);
        return Value{};
    }
    if (std::holds_alternative<double>(arg) || std::holds_alternative<ListValue>(arg) ||
        std::holds_alternative<MapValue>(arg) || std::holds_alternative<VertexValue>(arg) ||
        std::holds_alternative<EdgeValue>(arg) || std::holds_alternative<PathValue>(arg))
        throw std::runtime_error("TypeError: InvalidArgumentValue");
    return Value{};
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
