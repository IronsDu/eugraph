#pragma once

#include "query/dataset/row.hpp"
#include "query/function/function_def.hpp"

#include <cstdint>
#include <string>

namespace eugraph {
namespace function {
namespace aggregate {

/// Compare two Values for ordering. Returns true if a < b.
inline bool valueLess(const Value& a, const Value& b) {
    if (isNull(a) || isNull(b))
        return false;
    if (std::holds_alternative<bool>(a) && std::holds_alternative<bool>(b))
        return std::get<bool>(a) < std::get<bool>(b);
    if (std::holds_alternative<int64_t>(a) && std::holds_alternative<int64_t>(b))
        return std::get<int64_t>(a) < std::get<int64_t>(b);
    if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b))
        return std::get<double>(a) < std::get<double>(b);
    if (std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b))
        return std::get<std::string>(a) < std::get<std::string>(b);
    // Cross-type numeric comparison
    if (std::holds_alternative<int64_t>(a) && std::holds_alternative<double>(b))
        return static_cast<double>(std::get<int64_t>(a)) < std::get<double>(b);
    if (std::holds_alternative<double>(a) && std::holds_alternative<int64_t>(b))
        return std::get<double>(a) < static_cast<double>(std::get<int64_t>(b));
    return false; // incomparable types treated as equal
}

/// Running state for min/max aggregation.
template <bool IsMin> struct MinMaxState : AggStateBase {
    Value best; // monostate = uninitialized

    void add(const Value& arg) {
        if (isNull(arg))
            return;
        if (isNull(best)) {
            best = arg;
            return;
        }
        if constexpr (IsMin) {
            if (valueLess(arg, best))
                best = arg;
        } else {
            if (valueLess(best, arg))
                best = arg;
        }
    }
    Value finalize() const {
        return best;
    }
    void reset() {
        best = Value{};
    }
};

using MinState = MinMaxState<true>;
using MaxState = MinMaxState<false>;

} // namespace aggregate
} // namespace function
} // namespace eugraph
