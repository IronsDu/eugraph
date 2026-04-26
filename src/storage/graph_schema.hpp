#pragma once

#include "common/types/graph_types.hpp"

#include <optional>
#include <set>
#include <string>
#include <unordered_map>

namespace eugraph {

/// In-memory schema of the current database.
/// Maintained by AsyncGraphMetaStore, passed to data store during read/write operations.
/// Decouples data store from meta store — no need to share WT connections.
struct GraphSchema {
    // Label definitions
    std::unordered_map<LabelId, LabelDef> labels;
    std::unordered_map<std::string, LabelId> label_name_to_id;

    // Edge label definitions
    std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_labels;
    std::unordered_map<std::string, EdgeLabelId> edge_label_name_to_id;

    // Tombstone — deleted labels/edge labels
    std::set<LabelId> deleted_labels;
    std::set<EdgeLabelId> deleted_edge_labels;

    // ID counters
    LabelId next_label_id = 1;
    EdgeLabelId next_edge_label_id = 1;
    VertexId next_vertex_id = 1;
    EdgeId next_edge_id = 1;

    // Convenience accessors

    std::optional<LabelDef> getLabel(LabelId id) const {
        auto it = labels.find(id);
        if (it != labels.end())
            return it->second;
        return std::nullopt;
    }

    std::optional<LabelDef> getLabel(const std::string& name) const {
        auto it = label_name_to_id.find(name);
        if (it != label_name_to_id.end())
            return getLabel(it->second);
        return std::nullopt;
    }

    std::optional<EdgeLabelDef> getEdgeLabel(EdgeLabelId id) const {
        auto it = edge_labels.find(id);
        if (it != edge_labels.end())
            return it->second;
        return std::nullopt;
    }

    std::optional<EdgeLabelDef> getEdgeLabel(const std::string& name) const {
        auto it = edge_label_name_to_id.find(name);
        if (it != edge_label_name_to_id.end())
            return getEdgeLabel(it->second);
        return std::nullopt;
    }

    std::optional<LabelDef::IndexDef> findIndexByName(const std::string& index_name) const {
        for (const auto& [_, label] : labels) {
            for (const auto& idx : label.indexes) {
                if (idx.name == index_name)
                    return idx;
            }
        }
        for (const auto& [_, elabel] : edge_labels) {
            for (const auto& idx : elabel.indexes) {
                if (idx.name == index_name)
                    return idx;
            }
        }
        return std::nullopt;
    }
};

} // namespace eugraph
