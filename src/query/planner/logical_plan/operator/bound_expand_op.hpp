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

struct BoundExpandOp {
    std::string src_variable;
    uint32_t src_column_index;
    SlotId src_slot_id = INVALID_SLOT_ID;
    std::string edge_variable;
    uint32_t edge_column_index;
    SlotId edge_slot_id = INVALID_SLOT_ID;
    /// Planner-assigned slot captured in allocateSlotsInOp. After child
    /// processing, a descendant Project alias may overwrite name_to_slot
    /// with an unrelated same-name slot. A post-pass restores this slot
    /// for fresh bindings so canonical resolution doesn't merge the new
    /// binding with the alias's source (§6.2).
    SlotId planner_edge_slot_id = INVALID_SLOT_ID;
    std::string dst_variable;
    uint32_t dst_column_index;
    SlotId dst_slot_id = INVALID_SLOT_ID;
    SlotId planner_dst_slot_id = INVALID_SLOT_ID;
    std::vector<EdgeLabelId> edge_label_ids;
    cypher::RelationshipDirection direction;
    std::vector<uint16_t> edge_prop_ids;
    std::unordered_map<LabelId, std::vector<uint16_t>> dst_label_prop_ids;
    std::vector<LabelId> dst_label_ids;
    BoundLogicalOperator child;
};

} // namespace binder
} // namespace eugraph
