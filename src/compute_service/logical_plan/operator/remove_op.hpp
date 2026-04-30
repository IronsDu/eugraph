#pragma once

#include "compute_service/logical_plan/logical_operator_fwd.hpp"
#include "compute_service/parser/ast.hpp"

#include <vector>

namespace eugraph {
namespace compute {

struct RemoveOp {
    std::vector<cypher::RemoveItem> items;
    std::vector<LogicalOperator> children;
};

} // namespace compute
} // namespace eugraph
