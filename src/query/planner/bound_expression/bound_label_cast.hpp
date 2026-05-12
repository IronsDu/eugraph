#pragma once

#include "common/types/graph_types.hpp"
#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/bound_type.hpp"

namespace eugraph {
namespace binder {

/// Bound label-scoped access: n::Label
/// When followed by a property access (n::Label.prop), that property
/// reference will be strong-typed against the label.
struct BoundLabelCast {
    BoundExpression object;
    LabelId label_id = INVALID_LABEL_ID;
    BoundType result_type = BoundType::Vertex(); // scoped vertex value
};

} // namespace binder
} // namespace eugraph
