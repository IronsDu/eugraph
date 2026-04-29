#pragma once

#include "compute_service/logical_plan/logical_operator_fwd.hpp"

#include <string>
#include <vector>

namespace eugraph {
namespace compute {

struct AggregateFunc {
    std::string func_name; // "count", "sum", "avg", "min", "max"
    cypher::Expression arg;
    bool distinct = false;
};

struct AggregateOp {
    std::vector<cypher::Expression> group_keys;
    std::vector<AggregateFunc> aggregates;
    std::vector<std::string> output_names;
    std::vector<LogicalOperator> children;
};

} // namespace compute
} // namespace eugraph
