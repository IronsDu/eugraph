#pragma once

#include "query/planner/bound_expression/bound_expression_fwd.hpp"

#include <string>

namespace eugraph {
namespace binder {

/// Runtime property access by name. Unlike BoundPropertyRef which resolves
/// (label_id, prop_id) at bind time, this carries only the property name
/// and resolves at evaluation time by scanning all labels' property
/// definitions. Used as fallback when the property is not found in the
/// catalog (e.g., unlabeled nodes with __anon__ properties).
struct BoundDynamicPropertyRef {
    BoundExpression object;
    std::string property;
    BoundType result_type = BoundType::Any();
};

} // namespace binder
} // namespace eugraph
