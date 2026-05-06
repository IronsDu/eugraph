#pragma once

#include "compute_service/binder/bound_expression/bound_expression_fwd.hpp"
#include "compute_service/binder/bound_type.hpp"

#include <vector>

namespace eugraph {
namespace binder {

/// Bound list literal: [expr1, expr2, ...]
struct BoundList {
    std::vector<BoundExpression> elements;
    BoundType element_type = BoundType::Any();
    BoundType result_type; // List<element_type>

    BoundList() : result_type(BoundType::List(BoundType::Any())) {}
};

} // namespace binder
} // namespace eugraph
