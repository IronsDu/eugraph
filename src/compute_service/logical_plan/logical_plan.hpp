#pragma once

#include "compute_service/logical_plan/logical_operator.hpp"

#include <memory>
#include <string>

namespace eugraph {
namespace compute {

/// Forward declaration — implemented in logical_plan_builder.cpp
std::string logicalOperatorToString(const LogicalOperator& op, int indent = 0);

/// A logical plan is a tree of logical operators.
struct LogicalPlan {
    LogicalOperator root;

    std::string toString() const {
        return logicalOperatorToString(root);
    }
};

} // namespace compute
} // namespace eugraph
