#pragma once

#include "query/executor/row.hpp"
#include "query/function/function_def.hpp"

#include <cstdint>

namespace eugraph {
namespace function {
namespace aggregate {

/// Running state for avg aggregation.
/// Accumulates sum (double) and count, finalize returns average.
struct AvgState : AggStateBase {
    double sum = 0.0;
    int64_t count = 0;
    void add(const Value& arg) {
        if (std::holds_alternative<double>(arg)) {
            sum += std::get<double>(arg);
            ++count;
        } else if (std::holds_alternative<int64_t>(arg)) {
            sum += static_cast<double>(std::get<int64_t>(arg));
            ++count;
        }
    }
    Value finalize() const {
        if (count == 0)
            return Value{};
        return Value(sum / static_cast<double>(count));
    }
    void reset() {
        sum = 0.0;
        count = 0;
    }
};

} // namespace aggregate
} // namespace function
} // namespace eugraph
