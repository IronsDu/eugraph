#pragma once

#include "common/types/graph_types.hpp"

#include <string>
#include <string_view>
#include <optional>
#include <vector>
#include <functional>
#include <span>

namespace eugraph {

/// Opaque transaction handle — avoids exposing underlying engine types.
using GraphTxnHandle = void*;
static constexpr GraphTxnHandle INVALID_GRAPH_TXN = nullptr;

/// Graph-semantic storage interface.
/// Provides vertex/edge CRUD, label management, edge traversal, and primary key lookup.
/// Implementations translate these operations into KV reads/writes via IKVEngine.
class IGraphStore {
public:
    virtual ~IGraphStore() = default;

    // ==================== Lifecycle ====================

    virtual bool open(const std::string& db_path) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    // ==================== Transaction ====================

    virtual GraphTxnHandle beginTransaction() = 0;
    virtual bool commitTransaction(GraphTxnHandle txn) = 0;
    virtual bool rollbackTransaction(GraphTxnHandle txn) = 0;

    // ==================== Vertex ====================

    /// Insert a vertex.
    /// @param label_props  per-label properties (prop_id is independently numbered per label)
    /// @param pk_value     global primary key, nullptr means no primary key
    ///
    /// Writes: L| (label reverse), I| (label forward), X| (properties), P|/R| (primary key)
    virtual bool insertVertex(
        GraphTxnHandle txn,
        VertexId vid,
        std::span<const std::pair<LabelId, Properties>> label_props,
        const PropertyValue* pk_value) = 0;

    /// Delete a vertex and all associated data (label indexes, properties, primary key).
    /// Does NOT delete associated edges. Edge cleanup is the caller's responsibility.
    virtual bool deleteVertex(GraphTxnHandle txn, VertexId vid) = 0;

    // ==================== Vertex Properties ====================

    virtual std::optional<Properties> getVertexProperties(
        GraphTxnHandle txn, VertexId vid, LabelId label_id) = 0;

    virtual std::optional<PropertyValue> getVertexProperty(
        GraphTxnHandle txn, VertexId vid, LabelId label_id, uint16_t prop_id) = 0;

    virtual bool putVertexProperty(
        GraphTxnHandle txn, VertexId vid, LabelId label_id,
        uint16_t prop_id, const PropertyValue& value) = 0;

    /// Batch-set all properties for a vertex under a given label.
    /// Overwrites existing properties with the same (label, vid, prop_id) key.
    virtual bool putVertexProperties(
        GraphTxnHandle txn, VertexId vid, LabelId label_id,
        const Properties& props) = 0;

    virtual bool deleteVertexProperty(
        GraphTxnHandle txn, VertexId vid, LabelId label_id, uint16_t prop_id) = 0;

    // ==================== Vertex Labels ====================

    virtual LabelIdSet getVertexLabels(GraphTxnHandle txn, VertexId vid) = 0;

    /// Add a label to a vertex (writes L| and I| entries).
    virtual bool addVertexLabel(GraphTxnHandle txn, VertexId vid, LabelId label_id) = 0;

    /// Remove a label from a vertex (deletes L|, I|, and all X| properties under this label).
    virtual bool removeVertexLabel(GraphTxnHandle txn, VertexId vid, LabelId label_id) = 0;

    // ==================== Primary Key ====================

    virtual std::optional<VertexId> getVertexIdByPk(const PropertyValue& pk_value) = 0;
    virtual std::optional<PropertyValue> getPkByVertexId(VertexId vid) = 0;

    // ==================== Label Index Scan ====================

    /// Scan all vertex IDs with the given label (I| prefix scan).
    virtual void scanVerticesByLabel(
        GraphTxnHandle txn, LabelId label_id,
        const std::function<bool(VertexId)>& callback) = 0;

    // ==================== Edge ====================

    /// Insert an edge.
    /// @param seq  disambiguation ID for multiple edges of the same type between two vertices
    ///
    /// Writes: E| OUT index, E| IN index, D| edge properties
    virtual bool insertEdge(
        GraphTxnHandle txn, EdgeId eid,
        VertexId src_id, VertexId dst_id,
        EdgeLabelId label_id, uint64_t seq,
        const Properties& props) = 0;

    /// Delete an edge.
    /// Requires src/dst/seq to construct the E| keys for both directions.
    virtual bool deleteEdge(
        GraphTxnHandle txn, EdgeId eid, EdgeLabelId label_id,
        VertexId src_id, VertexId dst_id, uint64_t seq) = 0;

    // ==================== Edge Properties ====================

    virtual std::optional<Properties> getEdgeProperties(
        GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid) = 0;

    virtual std::optional<PropertyValue> getEdgeProperty(
        GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid, uint16_t prop_id) = 0;

    virtual bool putEdgeProperty(
        GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid,
        uint16_t prop_id, const PropertyValue& value) = 0;

    virtual bool deleteEdgeProperty(
        GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid, uint16_t prop_id) = 0;

    // ==================== Edge Traversal ====================

    struct EdgeIndexEntry {
        VertexId neighbor_id;
        EdgeLabelId edge_label_id;
        uint64_t seq;
        EdgeId edge_id;
    };

    /// Scan edges from a vertex by direction, optionally filtered by edge label.
    virtual void scanEdges(
        GraphTxnHandle txn, VertexId vid, Direction direction,
        std::optional<EdgeLabelId> label_filter,
        const std::function<bool(const EdgeIndexEntry&)>& callback) = 0;

    // ==================== Statistics ====================

    virtual uint64_t countVerticesByLabel(GraphTxnHandle txn, LabelId label_id) = 0;

    virtual uint64_t countDegree(
        GraphTxnHandle txn, VertexId vid, Direction direction,
        std::optional<EdgeLabelId> label_filter) = 0;
};

} // namespace eugraph
