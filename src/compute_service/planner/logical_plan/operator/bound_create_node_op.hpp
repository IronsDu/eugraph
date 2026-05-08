#pragma once

#include "common/types/graph_types.hpp"
#include "compute_service/planner/bound_expression/bound_expression_fwd.hpp"
#include "compute_service/planner/bound_logical_plan_fwd.hpp"

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
    std::vector<std::pair<uint16_t, BoundExpression>> properties;
    std::optional<BoundLogicalOperator> child;
};

} // namespace binder
} // namespace eugraph
