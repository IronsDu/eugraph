#pragma once

#include "compute_service/logical_plan/logical_operator_fwd.hpp"

#include <optional>
#include <string>
#include <vector>

namespace eugraph {
namespace compute {

/// Project / re-map columns.
struct ProjectOp {
    struct ProjectItem {
        cypher::Expression expr;
        std::optional<std::string> alias;
    };
    std::vector<ProjectItem> items;
    bool distinct = false;
    std::vector<LogicalOperator> children;
};

} // namespace compute
} // namespace eugraph
