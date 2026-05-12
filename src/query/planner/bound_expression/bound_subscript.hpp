#pragma once

#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/bound_type.hpp"

namespace eugraph {
namespace binder {

/// Bound subscript expression: list[index]
struct BoundSubscript {
    BoundExpression list;
    BoundExpression index;
    BoundType result_type = BoundType::Any();
};

} // namespace binder
} // namespace eugraph
