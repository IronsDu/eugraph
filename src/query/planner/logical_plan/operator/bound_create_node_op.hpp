#pragma once

#include "common/types/graph_types.hpp"
#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace eugraph {
namespace binder {

struct BoundCreateNodeOp {
    std::string variable;
    LabelId label_id;
    std::optional<std::string> label_name; // set when label doesn't exist (needs creation)
    std::vector<std::pair<uint16_t, BoundExpression>> properties;
    std::vector<std::pair<std::string, BoundExpression>> pending_props; // property names → expressions
    std::optional<BoundLogicalOperator> child;
};

} // namespace binder
} // namespace eugraph
