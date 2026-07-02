#pragma once

#include "common/types/graph_types.hpp"
#include "query/parser/ast.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace binder {

struct BoundExpandOp {
    std::string src_variable;
    uint32_t src_column_index;
    std::string edge_variable;
    uint32_t edge_column_index;
    std::string dst_variable;
    uint32_t dst_column_index;
    std::vector<EdgeLabelId> edge_label_ids;
    cypher::RelationshipDirection direction;
    std::vector<uint16_t> edge_prop_ids;
    std::unordered_map<LabelId, std::vector<uint16_t>> dst_label_prop_ids;
    std::vector<LabelId> dst_label_ids;
    BoundLogicalOperator child;
};

} // namespace binder
} // namespace eugraph
