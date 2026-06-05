#pragma once

#include "common/types/graph_types.hpp"
#include "query/parser/ast.hpp"
#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"
#include "query/planner/logical_plan/operator/bound_set_op.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace eugraph {
namespace binder {

struct BoundMergeOp {
    // Path binding
    std::optional<std::string> path_variable;

    // Start node pattern
    std::string start_var;
    bool start_pre_bound = false;
    std::vector<LabelId> start_labels;
    std::vector<std::pair<uint16_t, BoundExpression>> start_prop_filters;
    std::vector<std::pair<std::string, BoundExpression>> start_pending_props;

    // Relationship (optional)
    bool has_relationship = false;
    std::string edge_var;
    std::optional<EdgeLabelId> edge_label_id;
    std::optional<std::string> edge_label_name;
    cypher::RelationshipDirection direction = cypher::RelationshipDirection::LEFT_TO_RIGHT;
    std::vector<std::pair<uint16_t, BoundExpression>> edge_prop_filters;
    std::vector<std::pair<std::string, BoundExpression>> edge_pending_props;

    // End node (if has_relationship)
    std::string end_var;
    bool end_pre_bound = false;
    std::vector<LabelId> end_labels;
    std::vector<std::pair<uint16_t, BoundExpression>> end_prop_filters;
    std::vector<std::pair<std::string, BoundExpression>> end_pending_props;

    // ON CREATE / ON MATCH SET items
    std::vector<BoundSetOp::SetItem> on_create_items;
    std::vector<BoundSetOp::SetItem> on_match_items;

    // Child operator
    BoundLogicalOperator child;
};

} // namespace binder
} // namespace eugraph
