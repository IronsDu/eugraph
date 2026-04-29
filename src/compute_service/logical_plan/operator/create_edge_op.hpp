#pragma once

#include "compute_service/logical_plan/logical_operator_fwd.hpp"

#include <optional>
#include <string>
#include <vector>

namespace eugraph {
namespace compute {

/// Create an edge (relationship).
struct CreateEdgeOp {
    std::string variable;
    std::string src_variable;
    std::string dst_variable;
    std::vector<std::string> rel_types;
    cypher::RelationshipDirection direction;
    std::optional<cypher::PropertiesMap> properties;
    std::vector<LogicalOperator> children;
};

} // namespace compute
} // namespace eugraph
