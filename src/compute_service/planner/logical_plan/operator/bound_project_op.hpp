#pragma once

#include "compute_service/planner/bound_expression/bound_expression_fwd.hpp"
#include "compute_service/planner/bound_logical_plan_fwd.hpp"
#include "compute_service/planner/bound_type.hpp"

#include <string>
#include <vector>

namespace eugraph {
namespace binder {

struct BoundProjectOp {
    struct ProjectItem {
        BoundExpression expr;
        std::string alias;
        BoundType result_type;
    };
    std::vector<ProjectItem> items;
    BoundLogicalOperator child;
};

} // namespace binder
} // namespace eugraph
