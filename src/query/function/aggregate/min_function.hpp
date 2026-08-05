#pragma once

#include "query/dataset/row.hpp"
#include "query/function/compare_ops.hpp"
#include "query/function/function_def.hpp"

#include <cstdint>
#include <string>

namespace eugraph {
namespace function {
namespace aggregate {

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
        int cmp = compute::cypherCompareValues(arg, best);
        if constexpr (IsMin) {
            if (cmp < 0)
                best = arg;
        } else {
            if (cmp > 0)
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
