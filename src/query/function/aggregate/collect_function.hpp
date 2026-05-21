#pragma once

#include "query/dataset/row.hpp"
#include "query/function/function_def.hpp"

#include <vector>

namespace eugraph {
namespace function {
namespace aggregate {

struct CollectState : AggStateBase {
    std::vector<Value> values;

    void add(const Value& v) {
        if (!isNull(v)) {
            values.push_back(v);
        }
    }

    Value finalize() const {
        ListValue lv;
        lv.elements.reserve(values.size());
        for (const auto& v : values) {
            lv.elements.push_back(ValueStorage{v});
        }
        return Value(std::move(lv));
    }

    void reset() {
        values.clear();
    }
};

} // namespace aggregate
} // namespace function
} // namespace eugraph
