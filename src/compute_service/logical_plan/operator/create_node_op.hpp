#pragma once

#include "compute_service/logical_plan/logical_operator_fwd.hpp"

#include <optional>
#include <string>
#include <vector>

namespace eugraph {
namespace compute {

/// Create a node (vertex).
struct CreateNodeOp {
    std::string variable;                            // variable name
    std::vector<std::string> labels;                 // label names
    std::optional<cypher::PropertiesMap> properties; // initial properties
    std::vector<LogicalOperator> children;
};

} // namespace compute
} // namespace eugraph
