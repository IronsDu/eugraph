#pragma once

#include "compute_service/logical_plan/logical_operator_fwd.hpp"

#include <vector>

namespace eugraph {
namespace compute {

struct SortOp {
    std::vector<cypher::OrderBy::SortItem> sort_items;
    std::vector<LogicalOperator> children;
};

} // namespace compute
} // namespace eugraph
