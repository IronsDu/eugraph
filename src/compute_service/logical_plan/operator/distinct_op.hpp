#pragma once

#include "compute_service/logical_plan/logical_operator_fwd.hpp"

#include <vector>

namespace eugraph {
namespace compute {

struct DistinctOp {
    std::vector<LogicalOperator> children;
};

} // namespace compute
} // namespace eugraph
