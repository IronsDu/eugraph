#pragma once

#include "compute_service/logical_plan/logical_operator_fwd.hpp"

#include <cstdint>
#include <vector>

namespace eugraph {
namespace compute {

struct LimitOp {
    int64_t limit = 0;
    std::vector<LogicalOperator> children;
};

} // namespace compute
} // namespace eugraph
