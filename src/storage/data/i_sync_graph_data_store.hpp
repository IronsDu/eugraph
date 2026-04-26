#pragma once

#include "common/types/graph_types.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eugraph {

/// Synchronous graph data storage interface.
/// Provides vertex/edge CRUD, label management, edge traversal, primary key lookup, and DDL.
class ISyncGraphDataStore {
public:
    virtual ~ISyncGraphDataStore() = default;

    // ==================== Lifecycle ====================

    virtual bool open(const std::string& db_path) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    // ==================== Transaction ====================

    virtual GraphTxnHandle beginTransaction() = 0;
    virtual bool commitTransaction(GraphTxnHandle txn) = 0;
    virtual bool rollbackTransaction(GraphTxnHandle txn) = 0;

    // ==================== DDL ====================

    /// Create storage tables for a new label.
    virtual bool createLabel(LabelId label_id) = 0;

    /// Drop storage tables for a label.
    virtual bool dropLabel(LabelId label_id) = 0;

    /// Create storage tables for a new edge label.
    virtual bool createEdgeLabel(EdgeLabelId edge_label_id) = 0;

    /// Drop storage tables for an edge label.
    virtual bool dropEdgeLabel(EdgeLabelId edge_label_id) = 0;

    // ==================== Vertex ====================

    virtual bool insertVertex(GraphTxnHandle txn, VertexId vid,
                              std::span<const std::pair<LabelId, Properties>> label_props,
                              const PropertyValue* pk_value) = 0;
    virtual bool deleteVertex(GraphTxnHandle txn, VertexId vid) = 0;

    // ==================== Vertex Properties ====================

    virtual std::optional<Properties> getVertexProperties(GraphTxnHandle txn, VertexId vid, LabelId label_id) = 0;
    virtual std::optional<PropertyValue> getVertexProperty(GraphTxnHandle txn, VertexId vid, LabelId label_id,
                                                           uint16_t prop_id) = 0;
    virtual bool putVertexProperty(GraphTxnHandle txn, VertexId vid, LabelId label_id, uint16_t prop_id,
                                   const PropertyValue& value) = 0;
    virtual bool putVertexProperties(GraphTxnHandle txn, VertexId vid, LabelId label_id, const Properties& props) = 0;
    virtual bool deleteVertexProperty(GraphTxnHandle txn, VertexId vid, LabelId label_id, uint16_t prop_id) = 0;

    // ==================== Vertex Labels ====================

    virtual LabelIdSet getVertexLabels(GraphTxnHandle txn, VertexId vid) = 0;
    virtual bool addVertexLabel(GraphTxnHandle txn, VertexId vid, LabelId label_id) = 0;
    virtual bool removeVertexLabel(GraphTxnHandle txn, VertexId vid, LabelId label_id) = 0;

    // ==================== Primary Key ====================

    virtual std::optional<VertexId> getVertexIdByPk(const PropertyValue& pk_value) = 0;
    virtual std::optional<PropertyValue> getPkByVertexId(VertexId vid) = 0;

    // ==================== Edge Traversal Types ====================

    struct EdgeIndexEntry {
        VertexId neighbor_id;
        EdgeLabelId edge_label_id;
        uint64_t seq;
        EdgeId edge_id;
    };

    struct EdgeTypeIndexEntry {
        VertexId src_vertex_id;
        VertexId dst_vertex_id;
        uint64_t seq;
        EdgeId edge_id;
    };

    // ==================== Scan Cursors ====================

    class IVertexScanCursor {
    public:
        virtual ~IVertexScanCursor() = default;
        virtual bool valid() const = 0;
        virtual VertexId vertexId() const = 0;
        virtual void next() = 0;
    };

    class IEdgeScanCursor {
    public:
        virtual ~IEdgeScanCursor() = default;
        virtual bool valid() const = 0;
        virtual const EdgeIndexEntry& entry() const = 0;
        virtual void next() = 0;
    };

    class IEdgeTypeScanCursor {
    public:
        virtual ~IEdgeTypeScanCursor() = default;
        virtual bool valid() const = 0;
        virtual const EdgeTypeIndexEntry& entry() const = 0;
        virtual void next() = 0;
    };

    // ==================== Label Index Scan ====================

    virtual void scanVerticesByLabel(GraphTxnHandle txn, LabelId label_id,
                                     const std::function<bool(VertexId)>& callback) = 0;
    virtual std::unique_ptr<IVertexScanCursor> createVertexScanCursor(GraphTxnHandle txn, LabelId label_id) = 0;

    // ==================== Edge ====================

    virtual bool insertEdge(GraphTxnHandle txn, EdgeId eid, VertexId src_id, VertexId dst_id, EdgeLabelId label_id,
                            uint64_t seq, const Properties& props) = 0;
    virtual bool deleteEdge(GraphTxnHandle txn, EdgeId eid, EdgeLabelId label_id, VertexId src_id, VertexId dst_id,
                            uint64_t seq) = 0;

    // ==================== Edge Properties ====================

    virtual std::optional<Properties> getEdgeProperties(GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid) = 0;
    virtual std::optional<PropertyValue> getEdgeProperty(GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid,
                                                         uint16_t prop_id) = 0;
    virtual bool putEdgeProperty(GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid, uint16_t prop_id,
                                 const PropertyValue& value) = 0;
    virtual bool deleteEdgeProperty(GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid, uint16_t prop_id) = 0;

    // ==================== Edge Traversal ====================

    virtual void scanEdges(GraphTxnHandle txn, VertexId vid, Direction direction,
                           std::optional<EdgeLabelId> label_filter,
                           const std::function<bool(const EdgeIndexEntry&)>& callback) = 0;
    virtual std::unique_ptr<IEdgeScanCursor> createEdgeScanCursor(GraphTxnHandle txn, VertexId vid, Direction direction,
                                                                  std::optional<EdgeLabelId> label_filter) = 0;
    virtual void scanEdgesByType(GraphTxnHandle txn, EdgeLabelId label_id, std::optional<VertexId> src_filter,
                                 std::optional<VertexId> dst_filter,
                                 const std::function<bool(const EdgeTypeIndexEntry&)>& callback) = 0;
    virtual std::unique_ptr<IEdgeTypeScanCursor> createEdgeTypeScanCursor(GraphTxnHandle txn, EdgeLabelId label_id,
                                                                          std::optional<VertexId> src_filter,
                                                                          std::optional<VertexId> dst_filter) = 0;

    // ==================== Statistics ====================

    virtual uint64_t countVerticesByLabel(GraphTxnHandle txn, LabelId label_id) = 0;
    virtual uint64_t countDegree(GraphTxnHandle txn, VertexId vid, Direction direction,
                                 std::optional<EdgeLabelId> label_filter) = 0;

    // ==================== Index DDL ====================

    virtual bool createIndex(const std::string& table_name) = 0;
    virtual bool dropIndex(const std::string& table_name) = 0;

    // ==================== Index Entry Operations ====================

    virtual bool insertIndexEntry(const std::string& table, const PropertyValue& value, uint64_t entity_id) = 0;
    virtual bool deleteIndexEntry(const std::string& table, const PropertyValue& value, uint64_t entity_id) = 0;
    virtual bool checkUniqueConstraint(const std::string& table, const PropertyValue& value) = 0;

    // ==================== Index Scan ====================

    virtual void scanIndexEquality(GraphTxnHandle txn, const std::string& table, const PropertyValue& value,
                                   const std::function<bool(uint64_t)>& callback) = 0;
    virtual void scanIndexRange(GraphTxnHandle txn, const std::string& table, const std::optional<PropertyValue>& start,
                                const std::optional<PropertyValue>& end,
                                const std::function<bool(uint64_t)>& callback) = 0;

    // ==================== Index Cleanup ====================

    virtual void dropAllIndexEntries(const std::string& table) = 0;
};

} // namespace eugraph
