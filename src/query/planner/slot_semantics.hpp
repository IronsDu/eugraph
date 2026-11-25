#pragma once

#include "common/types/graph_types.hpp"
#include "query/planner/bound_type.hpp"
#include "query/planner/slot_id.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace binder {

enum class SlotSemanticKind {
    Scalar,
    Vertex,
    Edge,
    Property,
    List,
    Path,
};

/// Binder-era semantic view of a logical SlotId. This is the stable bridge
/// between BoundColumnRef (optimizer/explain) and catalog labels/properties.
struct SlotSemantics {
    SlotId slot = INVALID_SLOT_ID;
    SlotSemanticKind kind = SlotSemanticKind::Scalar;
    std::string name;
    BoundType type;

    std::vector<LabelId> vertex_labels;
    std::vector<EdgeLabelId> edge_labels;

    std::string property_name;
    std::vector<LabelId> property_labels;
    std::vector<uint16_t> property_ids;
    bool strong = false;

    SlotId source_slot = INVALID_SLOT_ID;
};

using SlotSemanticMap = std::unordered_map<SlotId, SlotSemantics>;

} // namespace binder
} // namespace eugraph
