#pragma once

#include "common/types/graph_types.hpp"
#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace eugraph {
namespace binder {

struct BoundCreateNodeOp {
    std::string variable;
    std::vector<LabelId> label_ids;
    /// Original label names from the AST. Used by the physical operator to
    /// auto-create labels that were not in the catalog snapshot at bind time
    /// (mirrors how BoundCreateEdgeOp carries label_name for auto-creation).
    std::vector<std::string> label_names;
    /// Per-label resolved properties: each entry maps one label to its (prop_id, expr) list.
    std::vector<std::pair<LabelId, std::vector<std::pair<uint16_t, BoundExpression>>>> label_properties;
    /// Unresolved properties (by name) — resolved at execution time against all labels.
    std::vector<std::pair<std::string, BoundExpression>> pending_props;
    std::optional<BoundLogicalOperator> child;
};

} // namespace binder
} // namespace eugraph
