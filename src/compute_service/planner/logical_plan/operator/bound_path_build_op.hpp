#pragma once

#include "compute_service/planner/bound_logical_plan_fwd.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace eugraph {
namespace binder {

struct BoundPathBuildOp {
    std::string path_variable;
    uint32_t path_column_index;
    std::vector<std::string> element_variables;
    BoundLogicalOperator child;
};

} // namespace binder
} // namespace eugraph
