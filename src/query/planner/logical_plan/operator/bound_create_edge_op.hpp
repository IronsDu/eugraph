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

struct BoundCreateEdgeOp {
    std::string variable;
    std::optional<EdgeLabelId> label_id;
    std::optional<std::string> label_name;
    std::string src_variable;
    std::string dst_variable;
    std::vector<std::pair<uint16_t, BoundExpression>> properties;
    std::vector<std::pair<std::string, BoundExpression>> pending_props;
    BoundLogicalOperator child;
};

} // namespace binder
} // namespace eugraph
