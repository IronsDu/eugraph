#include "query/planner/binder.hpp"

#include <spdlog/spdlog.h>

namespace eugraph {
namespace binder {

// ==================== Pattern Binding ====================

bool Binder::bindNodePattern(const cypher::NodePattern& node, std::string& var_name, uint32_t& col_idx,
                             std::vector<LabelId>& label_ids, std::vector<uint16_t>& /*default_prop_ids*/,
                             bool skip_register) {
    var_name = node.variable.value_or("");
    if (var_name.empty())
        var_name = "__anon_" + std::to_string(nextAnonId());

    if (skip_register) {
        auto* col = ctx_.lookup(var_name);
        col_idx = col ? col->column_index : nextColumnIndex();
    } else {
        col_idx = nextColumnIndex();
    }

    label_ids.clear();
    if (!node.labels.empty()) {
        for (const auto& label_name : node.labels) {
            LabelId lid = catalog_.labelNameToId(label_name);
            if (lid == INVALID_LABEL_ID) {
                error("Label '" + label_name + "' does not exist");
                return false;
            }
            if (std::find(label_ids.begin(), label_ids.end(), lid) == label_ids.end())
                label_ids.push_back(lid);
        }
    }

    if (!var_name.empty() && !skip_register) {
        ctx_.symbols[var_name] = makeColumnInfo(var_name, BoundType::Vertex(), label_ids);
    }

    return true;
}

bool Binder::bindRelationshipPattern(const cypher::RelationshipPattern& rel, std::string& var_name, uint32_t& col_idx,
                                     std::vector<EdgeLabelId>& edge_label_ids,
                                     std::vector<uint16_t>& /*default_prop_ids*/, bool for_create) {
    var_name = rel.variable.value_or("");
    if (var_name.empty())
        var_name = "__anon_edge_" + std::to_string(nextAnonId());
    col_idx = nextColumnIndex();

    edge_label_ids.clear();
    if (!rel.rel_types.empty()) {
        edge_label_ids = catalog_.resolveEdgeLabelIds(rel.rel_types);
        if (edge_label_ids.empty() && !for_create) {
            error("Edge type '" + rel.rel_types[0] + "' does not exist");
            return false;
        }
    } else {
        // No type filter: collect all edge labels
        for (const auto& [id, def] : catalog_.allEdgeLabels()) {
            edge_label_ids.push_back(id);
        }
    }

    if (!var_name.empty()) {
        ctx_.symbols[var_name] = makeColumnInfo(var_name, BoundType::Edge());
    }

    return true;
}

// ==================== Helpers ====================

BoundType Binder::propertyTypeToBoundType(PropertyType pt) {
    switch (pt) {
    case PropertyType::BOOL:
        return BoundType::Bool();
    case PropertyType::INT64:
        return BoundType::Int64();
    case PropertyType::DOUBLE:
        return BoundType::Double();
    case PropertyType::STRING:
        return BoundType::String();
    case PropertyType::INT64_ARRAY:
    case PropertyType::DOUBLE_ARRAY:
    case PropertyType::STRING_ARRAY:
        return BoundType::List(BoundType::Any()); // Simplified
    default:
        return BoundType::Any();
    }
}

ColumnInfo Binder::makeColumnInfo(const std::string& name, BoundType type, std::vector<LabelId> source_labels,
                                  std::optional<uint16_t> source_prop_id, bool strong_typed) {
    ColumnInfo info;
    info.name = name;
    info.type = std::move(type);
    info.column_index = ctx_.next_column_index > 0 ? ctx_.next_column_index - 1 : 0;
    info.source_labels = std::move(source_labels);
    info.source_prop_id = source_prop_id;
    info.strong_typed = strong_typed;
    return info;
}

} // namespace binder
} // namespace eugraph
