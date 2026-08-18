#pragma once

#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"

#include <cstdint>
#include <optional>

namespace eugraph {
namespace binder {

struct BoundLimitOp {
    /// Compile-time constant. Empty when the value must be evaluated at
    /// runtime (parameter or variable-free constant expression).
    std::optional<int64_t> constant;
    /// Runtime expression. Only valid when `constant` is empty. The binder
    /// guarantees the expression references no variables, so it can be
    /// evaluated once before pulling the child pipeline.
    std::optional<BoundExpression> expr;
    BoundLogicalOperator child;
};

} // namespace binder
} // namespace eugraph
