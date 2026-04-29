#pragma once

#include "compute_service/logical_plan/logical_operator_fwd.hpp"

#include <string>
#include <vector>

namespace eugraph {
namespace compute {

/// Scan vertices by label.
struct LabelScanOp {
    std::string variable;
    std::string label; // label name (string, resolved to ID later)
    std::vector<LogicalOperator> children;
};

} // namespace compute
} // namespace eugraph
