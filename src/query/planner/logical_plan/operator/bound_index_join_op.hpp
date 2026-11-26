#pragma once
#include "common/types/graph_types.hpp"
#include "query/parser/ast.hpp"
#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"
#include <optional>
#include <string>
#include <vector>
namespace eugraph {
namespace binder {
struct BoundIndexJoinOp {
    struct EdgeStep {
        std::string src_variable, dst_variable, edge_variable;
        std::vector<EdgeLabelId> edge_label_ids;
        cypher::RelationshipDirection direction = cypher::RelationshipDirection::UNDIRECTED;
        std::vector<LabelId> dst_label_ids;
    };
    BoundLogicalOperator left;
    std::string source_variable, target_variable;
    LabelId target_label = INVALID_LABEL_ID;
    uint32_t index_id = 0;
    uint16_t index_prop = 0;
    std::string index_property_name;
    std::optional<PropertyValue> equality, lower, upper;
    bool lower_inclusive = false;
    bool upper_inclusive = false;
    bool dynamic_key = false;
    std::string dynamic_key_variable;
    bool build_left = false;
    std::vector<EdgeStep> chain;
};
} // namespace binder
} // namespace eugraph
