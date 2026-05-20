#pragma once

#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"

#include <string>

namespace eugraph {
namespace binder {

struct BoundUnwindOp {
    BoundExpression list_expr;
    std::string variable;
    uint32_t variable_column_index;
    BoundLogicalOperator child;
};

} // namespace binder
} // namespace eugraph
