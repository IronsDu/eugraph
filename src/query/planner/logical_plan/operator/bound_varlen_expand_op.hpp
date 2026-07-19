#pragma once

#include "common/types/graph_types.hpp"
#include "query/parser/ast.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"
#include "query/planner/slot_id.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace binder {

struct BoundVarLenExpandOp {
    std::string src_variable;
    uint32_t src_column_index;
    std::string dst_variable;
    uint32_t dst_column_index;
    /// Binder-assigned slot for the dst variable. INVALID_SLOT_ID signals
    /// a fresh binding introduced by this operator (§6.2).
    SlotId dst_slot_id = INVALID_SLOT_ID;
    /// Planner-assigned slot captured in allocateSlotsInOp (§6.2).
    SlotId planner_dst_slot_id = INVALID_SLOT_ID;
    std::vector<EdgeLabelId> edge_label_ids;
    cypher::RelationshipDirection direction;
    int64_t min_hops;
    int64_t max_hops;
    std::unordered_map<LabelId, std::vector<uint16_t>> dst_label_prop_ids;
    std::vector<LabelId> dst_label_ids;
    // P1: named path variable (p = (a)-[*1..3]->(b))
    std::string path_variable;
    uint32_t path_column_index = 0;
    bool path_handled_by_varlen = false;
    // P2: named edge variable ([e:KNOWS*2]) → LIST<EDGE>
    std::string edge_variable;
    uint32_t edge_column_index = 0;
    /// Binder-assigned slot for the edge variable (§6.2).
    SlotId edge_slot_id = INVALID_SLOT_ID;
    /// Planner-assigned slot captured in allocateSlotsInOp (§6.2).
    SlotId planner_edge_slot_id = INVALID_SLOT_ID;
    // P3: inline edge property filter [{prop: value}]
    // Per edge-label: list of (prop_id, expected_value) equality checks
    std::unordered_map<EdgeLabelId, std::vector<std::pair<uint16_t, PropertyValue>>> edge_prop_filters;
    BoundLogicalOperator child;
};

} // namespace binder
} // namespace eugraph
