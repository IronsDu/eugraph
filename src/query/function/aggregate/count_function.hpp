#pragma once

#include "query/dataset/row.hpp"
#include "query/function/function_def.hpp"

#include <cstdint>

namespace eugraph {
namespace function {
namespace aggregate {

/// Running state for count aggregation.
struct CountState : AggStateBase {
    int64_t value = 0;
    void add(const Value& /*arg*/) {
        ++value;
    }
    Value finalize() const {
        return Value(value);
    }
    void reset() {
        value = 0;
    }
};

} // namespace aggregate
} // namespace function
} // namespace eugraph
