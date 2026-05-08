#pragma once

#include "compute_service/parser/ast.hpp"
#include "compute_service/planner/bound_expression/bound_expression_fwd.hpp"
#include "compute_service/planner/bound_logical_plan_fwd.hpp"

#include <vector>

namespace eugraph {
namespace binder {

struct BoundSortOp {
    struct SortItem {
        BoundExpression expr;
        cypher::OrderBy::Direction direction;
    };
    std::vector<SortItem> items;
    BoundLogicalOperator child;
};

} // namespace binder
} // namespace eugraph
