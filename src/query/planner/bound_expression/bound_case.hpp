#pragma once

#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/bound_type.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace eugraph {
namespace binder {

/// Bound CASE WHEN expression.
struct BoundCase {
    std::optional<BoundExpression> subject;
    std::vector<std::pair<BoundExpression, BoundExpression>> when_thens;
    std::optional<BoundExpression> else_expr;
    BoundType result_type = BoundType::Any();
};

} // namespace binder
} // namespace eugraph
