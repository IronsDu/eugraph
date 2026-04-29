#pragma once

#include "compute_service/logical_plan/logical_operator_fwd.hpp"

#include <cstdint>
#include <vector>

namespace eugraph {
namespace compute {

struct SkipOp {
    int64_t skip = 0;
    std::vector<LogicalOperator> children;
};

} // namespace compute
} // namespace eugraph
