#pragma once

#include "compute_service/function/function_def.hpp"
#include "compute_service/planner/bound_expression/bound_expression_fwd.hpp"
#include "compute_service/planner/bound_logical_plan_fwd.hpp"
#include "compute_service/planner/bound_type.hpp"

#include <string>
#include <vector>

namespace eugraph {
namespace binder {

struct BoundAggregateOp {
    struct AggregateItem {
        std::string function_name;
        BoundExpression argument;
        std::string alias;
        BoundType result_type;
        const function::FunctionDef* func_def;
        bool distinct = false;
    };
    std::vector<BoundExpression> group_keys;
    std::vector<AggregateItem> aggregates;
    std::vector<std::string> output_names;
    BoundLogicalOperator child;
};

} // namespace binder
} // namespace eugraph
