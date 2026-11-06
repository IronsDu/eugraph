#pragma once

#include "common/types/graph_types.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"

#include <string>
#include <vector>

namespace eugraph {
namespace binder {

/// Counts the final hop of a fixed OUT chain without materialising its edges.
///
/// The child is the prefix plan after N-1 hops. For each prefix row the final
/// hop contributes:
///
///   outdegree(current) - previous edges whose source == current
///
/// which preserves relationship uniqueness for the final edge.
struct BoundPathCountOp {
    std::string source_variable;
    std::string dst_variable;
    std::vector<std::string> previous_edge_variables;
    std::vector<EdgeLabelId> edge_label_ids;
    std::string output_name;
    BoundLogicalOperator child;
};

} // namespace binder
} // namespace eugraph
