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

} // namespace compute
} // namespace eugraph
