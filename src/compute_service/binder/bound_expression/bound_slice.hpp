#pragma once

#include "compute_service/binder/bound_expression/bound_expression_fwd.hpp"
#include "compute_service/binder/bound_type.hpp"

#include <optional>

namespace eugraph {
namespace binder {

/// Bound slice expression: list[from..to]
struct BoundSlice {
    BoundExpression list;
    std::optional<BoundExpression> from;
    std::optional<BoundExpression> to;
    BoundType result_type = BoundType::Any(); // List<element_type>
};

} // namespace binder
} // namespace eugraph
