#pragma once

#include "common/types/graph_types.hpp"
#include "query/parser/ast.hpp"

#include <optional>
#include <string>
#include <vector>

namespace eugraph {
namespace binder {

/// Index probe over a single edge step. This operator owns only the probe
/// (target index scan + reverse edge to source); it deliberately carries no
/// anchor subtree, so a separate rule decides which Group supplies the anchor.
struct BoundIndexProbeOp {
    std::string source_variable;
    std::string target_variable;
    std::string edge_variable;
    std::vector<EdgeLabelId> edge_label_ids;
    cypher::RelationshipDirection direction = cypher::RelationshipDirection::UNDIRECTED;
    std::vector<LabelId> target_label_ids;
    uint32_t index_id = 0;
    uint16_t index_prop = 0;
    std::string index_property_name;
    std::optional<PropertyValue> equality;
    std::optional<PropertyValue> lower;
    std::optional<PropertyValue> upper;
    bool lower_inclusive = false;
    bool upper_inclusive = false;
};

} // namespace binder
} // namespace eugraph
