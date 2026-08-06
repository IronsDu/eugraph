#pragma once

#include "query/function/function_def.hpp"
#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"
#include "query/planner/bound_type.hpp"

#include <string>
#include <vector>

namespace eugraph {
namespace binder {

struct BoundAggregateOp {
    struct AggregateItem {
        std::string function_name;
        /// Aggregate call arguments. Single-arg aggregates (count/sum/avg/min/
        /// max/collect) use args[0]; multi-arg aggregates (percentileDisc/
        /// percentileCont) use args[0]=value, args[1]=percentile.
        std::vector<BoundExpression> arguments;
        std::string alias;
        BoundType result_type;
        const function::FunctionDef* func_def;
        bool distinct = false;
        bool is_internal = false;
        bool keeps_nulls = false;
    };
    std::vector<BoundExpression> group_keys;
    std::vector<AggregateItem> aggregates;
    std::vector<std::string> output_names;
    BoundLogicalOperator child;
};

} // namespace binder
} // namespace eugraph
