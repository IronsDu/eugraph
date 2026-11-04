#pragma once

#include "query/dataset/row.hpp"
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
    void add(double arg) {
        sum += arg;
        ++count;
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
