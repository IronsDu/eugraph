#pragma once

#include "common/types/graph_types.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace catalog {

/// In-memory directory of all graph database objects.
///
/// Loaded from IAsyncGraphMetaStore at query preparation time.
/// Provides Binder with symbol resolution for labels, edge labels,
/// properties, and indexes.
class Catalog {
public:
    Catalog() = default;

    /// Build catalog from label and edge-label definitions.
    void load(std::unordered_map<LabelId, LabelDef> label_defs,
              std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_label_defs);

    // ── Label lookup ──

    const LabelDef* lookupLabel(const std::string& name) const;
    const LabelDef* lookupLabel(LabelId id) const;

    /// Look up multiple labels by name. Missing labels are excluded.
    std::vector<const LabelDef*> lookupLabels(const std::vector<std::string>& names) const;

    // ── EdgeLabel lookup ──

    const EdgeLabelDef* lookupEdgeLabel(const std::string& name) const;
    const EdgeLabelDef* lookupEdgeLabel(EdgeLabelId id) const;

    /// Look up type filter for Expand: names → ids (filters out missing).
    std::vector<EdgeLabelId> resolveEdgeLabelIds(const std::vector<std::string>& names) const;

    // ── Property lookup ──

    /// Find a property definition within a specific label.
    const PropertyDef* lookupProperty(LabelId lid, const std::string& prop_name) const;

    /// Find a property definition within a specific edge label.
    const PropertyDef* lookupEdgeLabelProperty(EdgeLabelId elid, const std::string& prop_name) const;

    /// Collect all PropertyDefs named `prop_name` across all labels in the set.
    /// Returns empty vector if not found in any label.
    /// Used for weak-type property access (n.name without ::Label).
    std::vector<std::pair<LabelId, const PropertyDef*>> lookupPropertyAcrossLabels(const LabelIdSet& label_ids,
                                                                                   const std::string& prop_name) const;

    // ── Index lookup ──

    const LabelDef::IndexDef* lookupIndex(LabelId lid, const std::string& index_name) const;

    // ── Bulk access ──

    const std::unordered_map<LabelId, LabelDef>& allLabels() const {
        return label_defs_;
    }
    const std::unordered_map<EdgeLabelId, EdgeLabelDef>& allEdgeLabels() const {
        return edge_label_defs_;
    }

    // ── Cardinality statistics (Phase 3 CBO) ──
    // Stored separately from LabelDef/EdgeLabelDef to avoid breaking
    // meta_codec serialization. Populated by storage layer; defaults to 0.

    void setLabelStats(LabelId lid, uint64_t vertex_count) {
        label_vertex_counts_[lid] = vertex_count;
    }
    void setEdgeLabelStats(EdgeLabelId elid, uint64_t edge_count, double avg_out_degree) {
        edge_label_counts_[elid] = edge_count;
        edge_avg_out_degrees_[elid] = avg_out_degree;
    }

    /// Returns vertex count for label, or 0 if unknown (caller applies default).
    uint64_t getVertexCount(LabelId lid) const {
        auto it = label_vertex_counts_.find(lid);
        return it != label_vertex_counts_.end() ? it->second : 0;
    }
    /// Returns avg out-degree for edge label, or 0.0 if unknown.
    double getAvgOutDegree(EdgeLabelId elid) const {
        auto it = edge_avg_out_degrees_.find(elid);
        return it != edge_avg_out_degrees_.end() ? it->second : 0.0;
    }
    /// Returns true if label has an index on the given property id.
    bool hasIndex(LabelId lid, uint16_t prop_id) const;

    // ── Name-id mappings ──

    LabelId labelNameToId(const std::string& name) const;
    EdgeLabelId edgeLabelNameToId(const std::string& name) const;

    /// Returns the internal __anon__ label ID (for unlabeled node properties).
    /// Returns INVALID_LABEL_ID if not initialized.
    LabelId getAnonLabelId() const {
        return anon_label_id_;
    }

private:
    std::unordered_map<std::string, LabelId> label_name_to_id_;
    std::unordered_map<LabelId, LabelDef> label_defs_;

    LabelId anon_label_id_ = INVALID_LABEL_ID;

    std::unordered_map<std::string, EdgeLabelId> edge_label_name_to_id_;
    std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_label_defs_;

    // Cardinality statistics (separate from LabelDef to avoid serialization changes)
    std::unordered_map<LabelId, uint64_t> label_vertex_counts_;
    std::unordered_map<EdgeLabelId, uint64_t> edge_label_counts_;
    std::unordered_map<EdgeLabelId, double> edge_avg_out_degrees_;
};

} // namespace catalog
} // namespace eugraph
