#pragma once

#include "common/types/constants.hpp"
#include "storage/graph_store.hpp"
#include "storage/kv/key_codec.hpp"
#include "storage/kv/value_codec.hpp"

#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

#include <wiredtiger.h>

namespace eugraph {

/// IGraphStore implementation using WiredTiger multi-table storage directly.
/// Each label gets its own tables (label_fwd_{id}, vprop_{id}).
/// Each edge label gets its own tables (etype_{id}, eprop_{id}).
/// DDL drop operations are implemented by dropping tables (near O(1)).
class GraphStoreImpl : public IGraphStore {
public:
    GraphStoreImpl() = default;
    ~GraphStoreImpl() override;

    GraphStoreImpl(const GraphStoreImpl&) = delete;
    GraphStoreImpl& operator=(const GraphStoreImpl&) = delete;

    // ==================== Lifecycle ====================

    bool open(const std::string& db_path) override;
    void close() override;
    bool isOpen() const override;

    // ==================== DDL ====================

    bool createLabel(LabelId label_id) override;
    bool dropLabel(LabelId label_id) override;
    bool createEdgeLabel(EdgeLabelId edge_label_id) override;
    bool dropEdgeLabel(EdgeLabelId edge_label_id) override;

    // ==================== Tombstone Filtering ====================

    void setDeletedLabelIds(std::set<LabelId> ids) override;
    void setDeletedEdgeLabelIds(std::set<EdgeLabelId> ids) override;

    // ==================== Transaction ====================

    GraphTxnHandle beginTransaction() override;
    bool commitTransaction(GraphTxnHandle txn) override;
    bool rollbackTransaction(GraphTxnHandle txn) override;

    // ==================== Vertex ====================

    bool insertVertex(GraphTxnHandle txn, VertexId vid, std::span<const std::pair<LabelId, Properties>> label_props,
                      const PropertyValue* pk_value) override;

    bool deleteVertex(GraphTxnHandle txn, VertexId vid) override;

    std::optional<Properties> getVertexProperties(GraphTxnHandle txn, VertexId vid, LabelId label_id) override;

    std::optional<PropertyValue> getVertexProperty(GraphTxnHandle txn, VertexId vid, LabelId label_id,
                                                   uint16_t prop_id) override;

    bool putVertexProperty(GraphTxnHandle txn, VertexId vid, LabelId label_id, uint16_t prop_id,
                           const PropertyValue& value) override;

    bool putVertexProperties(GraphTxnHandle txn, VertexId vid, LabelId label_id, const Properties& props) override;

    bool deleteVertexProperty(GraphTxnHandle txn, VertexId vid, LabelId label_id, uint16_t prop_id) override;

    LabelIdSet getVertexLabels(GraphTxnHandle txn, VertexId vid) override;
    bool addVertexLabel(GraphTxnHandle txn, VertexId vid, LabelId label_id) override;
    bool removeVertexLabel(GraphTxnHandle txn, VertexId vid, LabelId label_id) override;

    std::optional<VertexId> getVertexIdByPk(const PropertyValue& pk_value) override;
    std::optional<PropertyValue> getPkByVertexId(VertexId vid) override;

    void scanVerticesByLabel(GraphTxnHandle txn, LabelId label_id,
                             const std::function<bool(VertexId)>& callback) override;

    std::unique_ptr<IVertexScanCursor> createVertexScanCursor(GraphTxnHandle txn, LabelId label_id) override;

    // ==================== Edge ====================

    bool insertEdge(GraphTxnHandle txn, EdgeId eid, VertexId src_id, VertexId dst_id, EdgeLabelId label_id,
                    uint64_t seq, const Properties& props) override;

    bool deleteEdge(GraphTxnHandle txn, EdgeId eid, EdgeLabelId label_id, VertexId src_id, VertexId dst_id,
                    uint64_t seq) override;

    std::optional<Properties> getEdgeProperties(GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid) override;

    std::optional<PropertyValue> getEdgeProperty(GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid,
                                                 uint16_t prop_id) override;

    bool putEdgeProperty(GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid, uint16_t prop_id,
                         const PropertyValue& value) override;

    bool deleteEdgeProperty(GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid, uint16_t prop_id) override;

    void scanEdges(GraphTxnHandle txn, VertexId vid, Direction direction, std::optional<EdgeLabelId> label_filter,
                   const std::function<bool(const EdgeIndexEntry&)>& callback) override;

    std::unique_ptr<IEdgeScanCursor> createEdgeScanCursor(GraphTxnHandle txn, VertexId vid, Direction direction,
                                                          std::optional<EdgeLabelId> label_filter) override;

    void scanEdgesByType(GraphTxnHandle txn, EdgeLabelId label_id, std::optional<VertexId> src_filter,
                         std::optional<VertexId> dst_filter,
                         const std::function<bool(const EdgeTypeIndexEntry&)>& callback) override;

    std::unique_ptr<IEdgeTypeScanCursor> createEdgeTypeScanCursor(GraphTxnHandle txn, EdgeLabelId label_id,
                                                                  std::optional<VertexId> src_filter,
                                                                  std::optional<VertexId> dst_filter) override;

    uint64_t countVerticesByLabel(GraphTxnHandle txn, LabelId label_id) override;

    uint64_t countDegree(GraphTxnHandle txn, VertexId vid, Direction direction,
                         std::optional<EdgeLabelId> label_filter) override;

    // ==================== Metadata Raw KV ====================

    bool metadataPut(std::string_view key, std::string_view value) override;
    std::optional<std::string> metadataGet(std::string_view key) override;
    void metadataScan(std::string_view prefix,
                      const std::function<bool(std::string_view, std::string_view)>& callback) override;

private:
    // Per-transaction state: own session + cursor cache
    struct TxnState {
        WT_SESSION* session = nullptr;
        std::unordered_map<std::string, WT_CURSOR*> cursors;
    };

    // Get WT_SESSION for a transaction (or default session)
    WT_SESSION* getSession(GraphTxnHandle txn);

    // Get or create a cursor on a specific table for a session
    WT_CURSOR* getCursor(WT_SESSION* session, const std::string& table_name);

    // Table-level put/get/del helpers
    bool tablePut(WT_SESSION* session, const std::string& table, std::string_view key, std::string_view value);
    std::optional<std::string> tableGet(WT_SESSION* session, const std::string& table, std::string_view key);
    bool tableDel(WT_SESSION* session, const std::string& table, std::string_view key);

    // Table prefix scan
    void tableScan(WT_SESSION* session, const std::string& table, std::string_view prefix,
                   const std::function<bool(std::string_view, std::string_view)>& callback);

    // Close all cached cursors in a TxnState
    void closeTxnCursors(TxnState* state);

    // Ensure a global table exists
    bool ensureGlobalTable(WT_SESSION* session, const char* table_name);

    WT_CONNECTION* conn_ = nullptr;
    WT_SESSION* defaultSession_ = nullptr;

    std::mutex txnMutex_;
    std::unordered_map<GraphTxnHandle, std::unique_ptr<TxnState>> txns_;

    // Tombstone filter sets
    std::set<LabelId> deletedLabels_;
    std::set<EdgeLabelId> deletedEdgeLabels_;
};

// Scan cursor implementations directly using WT_CURSOR

class VertexScanCursorImpl : public IGraphStore::IVertexScanCursor {
public:
    VertexScanCursorImpl(WT_SESSION* session, const std::string& table_name);
    ~VertexScanCursorImpl() override;

    bool valid() const override;
    VertexId vertexId() const override;
    void next() override;

private:
    void readCurrent();

    WT_CURSOR* cursor_ = nullptr;
    WT_SESSION* session_ = nullptr;
    bool valid_ = false;
    VertexId current_vid_ = 0;
    std::string currentKey_;
};

class EdgeScanCursorImpl : public IGraphStore::IEdgeScanCursor {
public:
    EdgeScanCursorImpl(WT_SESSION* session, std::string_view prefix);
    ~EdgeScanCursorImpl() override;

    bool valid() const override;
    const IGraphStore::EdgeIndexEntry& entry() const override;
    void next() override;

private:
    void readCurrent();

    WT_CURSOR* cursor_ = nullptr;
    WT_SESSION* session_ = nullptr;
    std::string prefix_;
    bool valid_ = false;
    IGraphStore::EdgeIndexEntry current_;
    std::string currentKey_;
    std::string currentValue_;
};

class EdgeTypeScanCursorImpl : public IGraphStore::IEdgeTypeScanCursor {
public:
    EdgeTypeScanCursorImpl(WT_SESSION* session, const std::string& table_name, std::string_view prefix);
    ~EdgeTypeScanCursorImpl() override;

    bool valid() const override;
    const IGraphStore::EdgeTypeIndexEntry& entry() const override;
    void next() override;

private:
    void readCurrent();

    WT_CURSOR* cursor_ = nullptr;
    WT_SESSION* session_ = nullptr;
    std::string prefix_;
    bool valid_ = false;
    IGraphStore::EdgeTypeIndexEntry current_;
    std::string currentKey_;
    std::string currentValue_;
};

} // namespace eugraph
