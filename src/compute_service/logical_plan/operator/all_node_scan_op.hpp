#pragma once

#include "compute_service/logical_plan/logical_operator_fwd.hpp"

#include <string>
#include <vector>

namespace eugraph {
namespace compute {

/// Scan all vertices (no label filter).
struct AllNodeScanOp {
    std::string variable; // variable name bound to each vertex
    std::vector<LogicalOperator> children;
};

} // namespace compute
} // namespace eugraph
