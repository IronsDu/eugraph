#pragma once

#include "compute_service/executor/row.hpp"

#include <cstdint>

namespace eugraph {
namespace function {
namespace aggregate {

/// Running state for sum aggregation (int64).
struct Int64SumState {
    int64_t value = 0;
    void add(const Value& arg) {
        if (std::holds_alternative<int64_t>(arg))
            value += std::get<int64_t>(arg);
    }
    Value finalize() const {
        return Value(value);
    }
    void reset() {
        value = 0;
    }
};

/// Running state for sum aggregation (double).
struct DoubleSumState {
    double value = 0.0;
    void add(const Value& arg) {
        if (std::holds_alternative<double>(arg))
            value += std::get<double>(arg);
        else if (std::holds_alternative<int64_t>(arg))
            value += static_cast<double>(std::get<int64_t>(arg));
    }
    Value finalize() const {
        return Value(value);
    }
    void reset() {
        value = 0.0;
    }
};

} // namespace aggregate
} // namespace function
} // namespace eugraph
