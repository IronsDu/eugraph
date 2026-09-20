#pragma once

#include "common/types/query_error.hpp"
#include "query/dataset/row.hpp"
#include "query/function/function_def.hpp"
#include "query/function/scalar/support/typed_batch.hpp"

#include <algorithm>
#include <cctype>
#include <optional>

namespace eugraph {
namespace function {
namespace scalar {

inline Value ltrimImpl(const Value& arg);
inline Value rtrimImpl(const Value& arg);
inline Value toUpperImpl(const Value& arg);
inline Value toLowerImpl(const Value& arg);

// --- trim ---

inline Value trimImpl(const Value& arg) {
    if (isNull(arg))
        return Value{};
    if (!std::holds_alternative<std::string>(arg))
        return Value{};
    const auto& s = std::get<std::string>(arg);
    auto start = s.find_first_not_of(" \t\n\r\f\v");
    if (start == std::string::npos)
        return Value{std::string{}};
    auto end = s.find_last_not_of(" \t\n\r\f\v");
    return Value{s.substr(start, end - start + 1)};
}

struct TrimOp {
    static std::string apply(std::string v) {
        const Value out = trimImpl(Value(std::move(v)));
        return isNull(out) ? std::string{} : std::get<std::string>(out);
    }
};
struct LTrimOp {
    static std::string apply(std::string v) {
        const Value out = ltrimImpl(Value(std::move(v)));
        return isNull(out) ? std::string{} : std::get<std::string>(out);
    }
};
struct RTrimOp {
    static std::string apply(std::string v) {
        const Value out = rtrimImpl(Value(std::move(v)));
        return isNull(out) ? std::string{} : std::get<std::string>(out);
    }
};
struct UpperOp {
    static std::string apply(std::string v) {
        const Value out = toUpperImpl(Value(std::move(v)));
        return isNull(out) ? std::string{} : std::get<std::string>(out);
    }
};
struct LowerOp {
    static std::string apply(std::string v) {
        const Value out = toLowerImpl(Value(std::move(v)));
        return isNull(out) ? std::string{} : std::get<std::string>(out);
    }
};
struct ReverseStringOp {
    static std::string apply(std::string v) {
        std::reverse(v.begin(), v.end());
        return v;
    }
};

/// 两参数形式：从两端去掉 `chars` 里出现的任意字符（文档语义）。
/// 注意不要照抄 neo4j 5.26 的实现 —— 它的 trim(s, chars) 会直接返回第二个参数。
/// ltrim/rtrim 在 neo4j 里是正确的，语义与此一致。
inline Value trimCharsImpl(const Value& str_val, const Value& chars_val) {
    if (isNull(str_val) || isNull(chars_val))
        return Value{};
    if (!std::holds_alternative<std::string>(str_val) || !std::holds_alternative<std::string>(chars_val))
        return Value{};
    const auto& s = std::get<std::string>(str_val);
    const auto& chars = std::get<std::string>(chars_val);
    if (chars.empty())
        return Value{s};
    auto begin = s.find_first_not_of(chars);
    if (begin == std::string::npos)
        return Value{std::string{}};
    auto end = s.find_last_not_of(chars);
    return Value{s.substr(begin, end - begin + 1)};
}

inline Value ltrimCharsImpl(const Value& str_val, const Value& chars_val) {
    if (isNull(str_val) || isNull(chars_val))
        return Value{};
    if (!std::holds_alternative<std::string>(str_val) || !std::holds_alternative<std::string>(chars_val))
        return Value{};
    const auto& s = std::get<std::string>(str_val);
    const auto& chars = std::get<std::string>(chars_val);
    if (chars.empty())
        return Value{s};
    auto begin = s.find_first_not_of(chars);
    return Value{begin == std::string::npos ? std::string{} : s.substr(begin)};
}

inline Value rtrimCharsImpl(const Value& str_val, const Value& chars_val) {
    if (isNull(str_val) || isNull(chars_val))
        return Value{};
    if (!std::holds_alternative<std::string>(str_val) || !std::holds_alternative<std::string>(chars_val))
        return Value{};
    const auto& s = std::get<std::string>(str_val);
    const auto& chars = std::get<std::string>(chars_val);
    if (chars.empty())
        return Value{s};
    auto end = s.find_last_not_of(chars);
    return Value{end == std::string::npos ? std::string{} : s.substr(0, end + 1)};
}

inline void trimBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext&) {
    if (args.empty())
        return;
    const Column& in = *args[0];
    if (args.size() >= 2) {
        const Column& chars = *args[1];
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, trimCharsImpl(in.getValue(i), chars.getValue(i)));
        return;
    }
    if (in.type == binder::BoundTypeKind::STRING)
        typedUnaryBatch<std::string, std::string, TrimOp>(in, result, count);
    else
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, trimImpl(in.getValue(i)));
}

// --- ltrim ---

inline Value ltrimImpl(const Value& arg) {
    if (isNull(arg))
        return Value{};
    if (!std::holds_alternative<std::string>(arg))
        return Value{};
    const auto& s = std::get<std::string>(arg);
    auto start = s.find_first_not_of(" \t\n\r\f\v");
    if (start == std::string::npos)
        return Value{std::string{}};
    return Value{s.substr(start)};
}

inline void ltrimBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext&) {
    if (args.empty())
        return;
    const Column& in = *args[0];
    if (args.size() >= 2) {
        const Column& chars = *args[1];
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, ltrimCharsImpl(in.getValue(i), chars.getValue(i)));
        return;
    }
    if (in.type == binder::BoundTypeKind::STRING)
        typedUnaryBatch<std::string, std::string, LTrimOp>(in, result, count);
    else
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, ltrimImpl(in.getValue(i)));
}

// --- rtrim ---

inline Value rtrimImpl(const Value& arg) {
    if (isNull(arg))
        return Value{};
    if (!std::holds_alternative<std::string>(arg))
        return Value{};
    const auto& s = std::get<std::string>(arg);
    auto end = s.find_last_not_of(" \t\n\r\f\v");
    if (end == std::string::npos)
        return Value{std::string{}};
    return Value{s.substr(0, end + 1)};
}

inline void rtrimBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext&) {
    if (args.empty())
        return;
    const Column& in = *args[0];
    if (args.size() >= 2) {
        const Column& chars = *args[1];
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, rtrimCharsImpl(in.getValue(i), chars.getValue(i)));
        return;
    }
    if (in.type == binder::BoundTypeKind::STRING)
        typedUnaryBatch<std::string, std::string, RTrimOp>(in, result, count);
    else
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, rtrimImpl(in.getValue(i)));
}

// --- split ---

inline Value splitImpl(const Value& str_val, const Value& sep_val) {
    if (isNull(str_val) || isNull(sep_val))
        return Value{};
    if (!std::holds_alternative<std::string>(str_val) || !std::holds_alternative<std::string>(sep_val))
        return Value{};
    const auto& s = std::get<std::string>(str_val);
    const auto& sep = std::get<std::string>(sep_val);
    if (sep.empty())
        return Value{};
    ListValue lv;
    size_t pos = 0;
    while (pos <= s.size()) {
        auto next = s.find(sep, pos);
        if (next == std::string::npos) {
            lv.elements.push_back(ValueStorage{Value{s.substr(pos)}});
            break;
        }
        lv.elements.push_back(ValueStorage{Value{s.substr(pos, next - pos)}});
        pos = next + sep.size();
    }
    return Value(mk<ListValue>(std::move(lv)));
}

inline void splitBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext&) {
    if (args.size() < 2)
        return;
    const auto& str_col = *args[0];
    const auto& sep_col = *args[1];
    for (size_t i = 0; i < count; ++i)
        result.setValue(i, splitImpl(str_col.getValue(i), sep_col.getValue(i)));
}

// --- replace ---

inline Value replaceImpl(const Value& str_val, const Value& old_val, const Value& new_val) {
    if (isNull(str_val) || isNull(old_val) || isNull(new_val))
        return Value{};
    if (!std::holds_alternative<std::string>(str_val) || !std::holds_alternative<std::string>(old_val) ||
        !std::holds_alternative<std::string>(new_val))
        return Value{};
    auto s = std::get<std::string>(str_val);
    const auto& old_s = std::get<std::string>(old_val);
    const auto& new_s = std::get<std::string>(new_val);
    if (old_s.empty())
        return Value{std::move(s)};
    size_t pos = 0;
    while ((pos = s.find(old_s, pos)) != std::string::npos) {
        s.replace(pos, old_s.size(), new_s);
        pos += new_s.size();
    }
    return Value{std::move(s)};
}

inline void replaceBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext&) {
    if (args.size() < 3)
        return;
    const auto& s_col = *args[0];
    const auto& old_col = *args[1];
    const auto& new_col = *args[2];
    for (size_t i = 0; i < count; ++i)
        result.setValue(i, replaceImpl(s_col.getValue(i), old_col.getValue(i), new_col.getValue(i)));
}

// --- substring ---

/// `length` 为 nullopt 表示两参数形式（取到字符串末尾）。
/// 负的起始位置或负的长度是错误（neo4j: Neo.DatabaseError.Statement.ExecutionFailed），
/// 而不是返回 NULL —— 静默的 NULL 会让调用方把「参数写错」当成「没有值」。
inline Value substringImpl(const Value& str_val, int64_t start, std::optional<int64_t> length) {
    if (isNull(str_val))
        return Value{};
    if (!std::holds_alternative<std::string>(str_val))
        return Value{};
    if (start < 0 || (length.has_value() && *length < 0))
        throw QueryException(QueryErrorKind::ExecutionFailed, "Cannot handle negative start index nor negative length");
    const auto& s = std::get<std::string>(str_val);
    auto ustart = static_cast<size_t>(start);
    if (ustart >= s.size())
        return Value{std::string{}};
    const auto remaining = s.size() - ustart;
    const auto ulen = length.has_value() ? std::min(static_cast<size_t>(*length), remaining) : remaining;
    return Value{s.substr(ustart, ulen)};
}

inline void substringBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext&) {
    if (args.size() < 2)
        return;
    const auto& str_col = *args[0];
    const auto& start_col = *args[1];
    const Column* len_col = args.size() >= 3 ? args[2] : nullptr;
    for (size_t i = 0; i < count; ++i) {
        auto start_val = start_col.getValue(i);
        if (isNull(str_col.getValue(i)) || isNull(start_val)) {
            result.setValue(i, Value{});
            continue;
        }
        auto start = std::get<int64_t>(start_val);
        std::optional<int64_t> length;
        if (len_col) {
            auto len_val = len_col->getValue(i);
            if (isNull(len_val)) {
                // 长度显式为 NULL：结果也是 NULL（与 neo4j 一致），不再当作「取到末尾」。
                result.setValue(i, Value{});
                continue;
            }
            length = std::get<int64_t>(len_val);
        }
        result.setValue(i, substringImpl(str_col.getValue(i), start, length));
    }
}

// --- left ---

inline Value leftImpl(const Value& str_val, int64_t n) {
    if (isNull(str_val))
        return Value{};
    if (!std::holds_alternative<std::string>(str_val))
        return Value{};
    const auto& s = std::get<std::string>(str_val);
    if (n < 0)
        return Value{std::string{}};
    auto un = static_cast<size_t>(n);
    if (un > s.size())
        un = s.size();
    return Value{s.substr(0, un)};
}

inline void leftBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext&) {
    if (args.size() < 2)
        return;
    const auto& str_col = *args[0];
    const auto& n_col = *args[1];
    for (size_t i = 0; i < count; ++i) {
        auto n_val = n_col.getValue(i);
        if (isNull(str_col.getValue(i)) || isNull(n_val)) {
            result.setValue(i, Value{});
            continue;
        }
        result.setValue(i, leftImpl(str_col.getValue(i), std::get<int64_t>(n_val)));
    }
}

// --- right ---

inline Value rightImpl(const Value& str_val, int64_t n) {
    if (isNull(str_val))
        return Value{};
    if (!std::holds_alternative<std::string>(str_val))
        return Value{};
    const auto& s = std::get<std::string>(str_val);
    if (n < 0)
        return Value{std::string{}};
    auto un = static_cast<size_t>(n);
    if (un > s.size())
        un = s.size();
    return Value{s.substr(s.size() - un)};
}

inline void rightBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext&) {
    if (args.size() < 2)
        return;
    const auto& str_col = *args[0];
    const auto& n_col = *args[1];
    for (size_t i = 0; i < count; ++i) {
        auto n_val = n_col.getValue(i);
        if (isNull(str_col.getValue(i)) || isNull(n_val)) {
            result.setValue(i, Value{});
            continue;
        }
        result.setValue(i, rightImpl(str_col.getValue(i), std::get<int64_t>(n_val)));
    }
}

// --- toUpper ---

inline Value toUpperImpl(const Value& arg) {
    if (isNull(arg))
        return Value{};
    if (!std::holds_alternative<std::string>(arg))
        return Value{};
    std::string s = std::get<std::string>(arg);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
    return Value{std::move(s)};
}

inline void toUpperBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext&) {
    if (args.empty())
        return;
    const Column& in = *args[0];
    if (in.type == binder::BoundTypeKind::STRING)
        typedUnaryBatch<std::string, std::string, UpperOp>(in, result, count);
    else
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, toUpperImpl(in.getValue(i)));
}

// --- toLower ---

inline Value toLowerImpl(const Value& arg) {
    if (isNull(arg))
        return Value{};
    if (!std::holds_alternative<std::string>(arg))
        return Value{};
    std::string s = std::get<std::string>(arg);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return Value{std::move(s)};
}

inline void toLowerBatchFn(const std::vector<const Column*>& args, Column& result, size_t count, const EvalContext&) {
    if (args.empty())
        return;
    const Column& in = *args[0];
    if (in.type == binder::BoundTypeKind::STRING)
        typedUnaryBatch<std::string, std::string, LowerOp>(in, result, count);
    else
        for (size_t i = 0; i < count; ++i)
            result.setValue(i, toLowerImpl(in.getValue(i)));
}

} // namespace scalar
} // namespace function
} // namespace eugraph
