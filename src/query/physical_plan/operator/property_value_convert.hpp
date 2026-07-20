#pragma once

#include "common/types/graph_types.hpp"
#include "query/dataset/row.hpp"

namespace eugraph {
namespace compute {

/// Convert a storage-layer PropertyValue to a runtime Value.
/// Scalars pass through; arrays are wrapped in ListValue.
inline Value propertyValueToValue(const PropertyValue& pv) {
    return std::visit(
        [](const auto& arg) -> Value {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>)
                return Value{};
            else if constexpr (std::is_same_v<T, std::vector<int64_t>>) {
                ListValue lv;
                for (auto v : arg)
                    lv.elements.push_back(ValueStorage{Value(v)});
                return Value(std::move(lv));
            } else if constexpr (std::is_same_v<T, std::vector<double>>) {
                ListValue lv;
                for (auto v : arg)
                    lv.elements.push_back(ValueStorage{Value(v)});
                return Value(std::move(lv));
            } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                ListValue lv;
                for (auto v : arg)
                    lv.elements.push_back(ValueStorage{Value(std::move(v))});
                return Value(std::move(lv));
            } else if constexpr (std::is_same_v<T, std::vector<DateTimeValue>>) {
                ListValue lv;
                for (auto& v : arg)
                    lv.elements.push_back(ValueStorage{Value(v)});
                return Value(std::move(lv));
            } else if constexpr (std::is_same_v<T, std::vector<TimeValue>>) {
                ListValue lv;
                for (auto& v : arg)
                    lv.elements.push_back(ValueStorage{Value(v)});
                return Value(std::move(lv));
            } else if constexpr (std::is_same_v<T, std::vector<DurationValue>>) {
                ListValue lv;
                for (auto& v : arg)
                    lv.elements.push_back(ValueStorage{Value(v)});
                return Value(std::move(lv));
            } else
                return Value(arg);
        },
        pv);
}

/// Convert a runtime Value to a storage-layer PropertyValue.
///
/// Scalars pass through; ListValue is unrolled into the typed vector variant
/// matching its first element (homogeneous lists only). Empty lists, lists
/// with heterogeneous element types, and unsupported Value variants fall
/// through to monostate — historically the source of silent data corruption
/// (property written as null, no runtime error). Callers that need to
/// distinguish "null" from "unsupported" must check the input Value variant
/// before calling.
inline PropertyValue valueToPropertyValue(const Value& v) {
    if (std::holds_alternative<bool>(v))
        return std::get<bool>(v);
    if (std::holds_alternative<int64_t>(v))
        return std::get<int64_t>(v);
    if (std::holds_alternative<double>(v))
        return std::get<double>(v);
    if (std::holds_alternative<std::string>(v))
        return std::get<std::string>(v);
    if (std::holds_alternative<DateTimeValue>(v))
        return std::get<DateTimeValue>(v);
    if (std::holds_alternative<TimeValue>(v))
        return std::get<TimeValue>(v);
    if (std::holds_alternative<DurationValue>(v))
        return std::get<DurationValue>(v);
    if (std::holds_alternative<ListValue>(v)) {
        const auto& lv = std::get<ListValue>(v);
        if (lv.elements.empty())
            return PropertyValue{};
        const auto& first = lv.elements[0].value;
        if (std::holds_alternative<int64_t>(first)) {
            std::vector<int64_t> arr;
            for (const auto& e : lv.elements)
                if (std::holds_alternative<int64_t>(e.value))
                    arr.push_back(std::get<int64_t>(e.value));
            if (arr.size() == lv.elements.size())
                return arr;
        } else if (std::holds_alternative<double>(first)) {
            std::vector<double> arr;
            for (const auto& e : lv.elements)
                if (std::holds_alternative<double>(e.value))
                    arr.push_back(std::get<double>(e.value));
            if (arr.size() == lv.elements.size())
                return arr;
        } else if (std::holds_alternative<std::string>(first)) {
            std::vector<std::string> arr;
            for (const auto& e : lv.elements)
                if (std::holds_alternative<std::string>(e.value))
                    arr.push_back(std::get<std::string>(e.value));
            if (arr.size() == lv.elements.size())
                return arr;
        } else if (std::holds_alternative<DateTimeValue>(first)) {
            std::vector<DateTimeValue> arr;
            for (const auto& e : lv.elements)
                if (std::holds_alternative<DateTimeValue>(e.value))
                    arr.push_back(std::get<DateTimeValue>(e.value));
            if (arr.size() == lv.elements.size())
                return arr;
        } else if (std::holds_alternative<TimeValue>(first)) {
            std::vector<TimeValue> arr;
            for (const auto& e : lv.elements)
                if (std::holds_alternative<TimeValue>(e.value))
                    arr.push_back(std::get<TimeValue>(e.value));
            if (arr.size() == lv.elements.size())
                return arr;
        } else if (std::holds_alternative<DurationValue>(first)) {
            std::vector<DurationValue> arr;
            for (const auto& e : lv.elements)
                if (std::holds_alternative<DurationValue>(e.value))
                    arr.push_back(std::get<DurationValue>(e.value));
            if (arr.size() == lv.elements.size())
                return arr;
        }
    }
    return PropertyValue{};
}

} // namespace compute
} // namespace eugraph
