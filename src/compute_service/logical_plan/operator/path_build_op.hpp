#pragma once

#include "compute_service/logical_plan/logical_operator_fwd.hpp"

#include <string>
#include <vector>

namespace eugraph {
namespace compute {

/// Assembles path elements (vertices and edges) from existing row columns into a PathValue.
struct PathBuildOp {
    std::string path_variable;                  // output variable name (e.g., "p")
    std::vector<std::string> element_variables; // ordered element vars: v1, e1, v2, e2, ...
    std::vector<LogicalOperator> children;
};

} // namespace compute
} // namespace eugraph
