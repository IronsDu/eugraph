#pragma once

#include "compute_service/logical_plan/logical_operator_fwd.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace eugraph {
namespace compute {

/// Expand from a vertex variable to find neighbors via edges.
struct ExpandOp {
    std::string src_variable;           // source vertex variable
    std::string dst_variable;           // destination vertex variable (new binding)
    std::string edge_variable;          // optional edge variable
    std::vector<std::string> rel_types; // relationship type names
    cypher::RelationshipDirection direction;
    std::optional<std::pair<cypher::Expression, cypher::Expression>> range; // *min..max
    std::vector<LogicalOperator> children;
};

} // namespace compute
} // namespace eugraph
