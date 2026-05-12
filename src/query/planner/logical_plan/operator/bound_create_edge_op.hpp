#pragma once

#include "common/types/graph_types.hpp"
#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace eugraph {
namespace binder {

struct BoundCreateEdgeOp {
    std::string variable;
    EdgeLabelId label_id;
    std::string src_variable;
    std::string dst_variable;
    std::vector<std::pair<uint16_t, BoundExpression>> properties;
    BoundLogicalOperator child;
};

} // namespace binder
} // namespace eugraph
