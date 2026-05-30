#pragma once

#include "common/types/graph_types.hpp"
#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/bound_type.hpp"

#include <cstdint>
#include <vector>

namespace eugraph {
namespace binder {

/// Bound property access: resolved to prop_id(s) and type.
///
/// For weak-type access (n.name), there may be multiple candidates
/// across different labels. If all candidates have the same type,
/// result_type is that type; otherwise it is ANY.
///
/// For strong-type access (n::Person.name), there is exactly one
/// candidate with a definite type.
struct BoundPropertyRef {
    /// The object whose property is being accessed.
    /// The property name (used for structural field access like r.id, n.labels).
    std::string property_name;

    /// The object whose property is being accessed.
    BoundExpression object;

    /// Information about a resolved property candidate.
    struct ResolvedProperty {
        LabelId label_id = INVALID_LABEL_ID;
        uint16_t prop_id = 0;
        BoundType type = BoundType::Any();
    };

    std::vector<ResolvedProperty> candidates;
    BoundType result_type = BoundType::Any();

    bool isStrongTyped() const {
        return candidates.size() == 1;
    }
};

} // namespace binder
} // namespace eugraph
