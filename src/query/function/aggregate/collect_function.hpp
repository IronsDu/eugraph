#pragma once

#include "query/dataset/row.hpp"
#include "query/function/function_def.hpp"

#include <vector>

namespace eugraph {
namespace function {
namespace aggregate {

struct CollectState : AggStateBase {
    std::vector<Value> values;

    /// Per openCypher TCK (Pattern2 [4], Aggregation8), collect() includes
    /// null values in the resulting list. The aggregate physical operator
    /// routes null inputs here only because collect's FunctionDef has
    /// keeps_nulls=true; for non-DISTINCT collect the nulls must end up in
    /// the output. DISTINCT collect filters nulls at the physical op (the
    /// keeps_nulls flag does not bypass DISTINCT's dedup).
    void add(const Value& v) {
        values.push_back(v);
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
