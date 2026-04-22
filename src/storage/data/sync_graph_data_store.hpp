#pragma once

#include "common/types/constants.hpp"
#include "storage/data/i_sync_graph_data_store.hpp"
#include "storage/kv/key_codec.hpp"
#include "storage/kv/value_codec.hpp"
#include "storage/wt_store_base.hpp"

#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

#include <wiredtiger.h>

namespace eugraph {

/// ISyncGraphDataStore implementation using WiredTiger multi-table storage.
/// Owns its WT connection for data tables.
class SyncGraphDataStore : public WtStoreBase, public ISyncGraphDataStore {
public:
    SyncGraphDataStore() = default;
    ~SyncGraphDataStore() override;

    SyncGraphDataStore(const SyncGraphDataStore&) = delete;
    SyncGraphDataStore& operator=(const SyncGraphDataStore&) = delete;

    // Lifecycle
    bool open(const std::string& db_path) override;
    void close() override;
    bool isOpen() const override;

    // Transaction
    GraphTxnHandle beginTransaction() override;
    bool commitTransaction(GraphTxnHandle txn) override;
    bool rollbackTransaction(GraphTxnHandle txn) override;

    // DDL
    bool createLabel(LabelId label_id) override;
    bool dropLabel(LabelId label_id) override;
    bool createEdgeLabel(EdgeLabelId edge_label_id) override;
    bool dropEdgeLabel(EdgeLabelId edge_label_id) override;

    // Vertex
    bool insertVertex(GraphTxnHandle txn, VertexId vid, std::span<const std::pair<LabelId, Properties>> label_props,
                      const PropertyValue* pk_value) override;
    bool deleteVertex(GraphTxnHandle txn, VertexId vid) override;

    // Vertex Properties
    std::optional<Properties> getVertexProperties(GraphTxnHandle txn, VertexId vid, LabelId label_id) override;
    std::optional<PropertyValue> getVertexProperty(GraphTxnHandle txn, VertexId vid, LabelId label_id,
                                                   uint16_t prop_id) override;
    bool putVertexProperty(GraphTxnHandle txn, VertexId vid, LabelId label_id, uint16_t prop_id,
                           const PropertyValue& value) override;
    bool putVertexProperties(GraphTxnHandle txn, VertexId vid, LabelId label_id, const Properties& props) override;
    bool deleteVertexProperty(GraphTxnHandle txn, VertexId vid, LabelId label_id, uint16_t prop_id) override;

    // Vertex Labels
    LabelIdSet getVertexLabels(GraphTxnHandle txn, VertexId vid) override;
    bool addVertexLabel(GraphTxnHandle txn, VertexId vid, LabelId label_id) override;
    bool removeVertexLabel(GraphTxnHandle txn, VertexId vid, LabelId label_id) override;

    // Primary Key
    std::optional<VertexId> getVertexIdByPk(const PropertyValue& pk_value) override;
    std::optional<PropertyValue> getPkByVertexId(VertexId vid) override;

    // Label Index Scan
    void scanVerticesByLabel(GraphTxnHandle txn, LabelId label_id,
                             const std::function<bool(VertexId)>& callback) override;
    std::unique_ptr<IVertexScanCursor> createVertexScanCursor(GraphTxnHandle txn, LabelId label_id) override;

    // Edge
    bool insertEdge(GraphTxnHandle txn, EdgeId eid, VertexId src_id, VertexId dst_id, EdgeLabelId label_id,
                    uint64_t seq, const Properties& props) override;
    bool deleteEdge(GraphTxnHandle txn, EdgeId eid, EdgeLabelId label_id, VertexId src_id, VertexId dst_id,
                    uint64_t seq) override;

    // Edge Properties
    std::optional<Properties> getEdgeProperties(GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid) override;
    std::optional<PropertyValue> getEdgeProperty(GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid,
                                                 uint16_t prop_id) override;
    bool putEdgeProperty(GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid, uint16_t prop_id,
                         const PropertyValue& value) override;
    bool deleteEdgeProperty(GraphTxnHandle txn, EdgeLabelId label_id, EdgeId eid, uint16_t prop_id) override;

    // Edge Traversal
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

    // Statistics
    uint64_t countVerticesByLabel(GraphTxnHandle txn, LabelId label_id) override;
    uint64_t countDegree(GraphTxnHandle txn, VertexId vid, Direction direction,
                         std::optional<EdgeLabelId> label_filter) override;
};

// ==================== Scan Cursor Implementations ====================

class VertexScanCursorImpl : public ISyncGraphDataStore::IVertexScanCursor {
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

class EdgeScanCursorImpl : public ISyncGraphDataStore::IEdgeScanCursor {
public:
    EdgeScanCursorImpl(WT_SESSION* session, std::string_view prefix);
    ~EdgeScanCursorImpl() override;

    bool valid() const override;
    const ISyncGraphDataStore::EdgeIndexEntry& entry() const override;
    void next() override;

private:
    void readCurrent();

    WT_CURSOR* cursor_ = nullptr;
    WT_SESSION* session_ = nullptr;
    std::string prefix_;
    bool valid_ = false;
    ISyncGraphDataStore::EdgeIndexEntry current_;
    std::string currentKey_;
    std::string currentValue_;
};

class EdgeTypeScanCursorImpl : public ISyncGraphDataStore::IEdgeTypeScanCursor {
public:
    EdgeTypeScanCursorImpl(WT_SESSION* session, const std::string& table_name, std::string_view prefix);
    ~EdgeTypeScanCursorImpl() override;

    bool valid() const override;
    const ISyncGraphDataStore::EdgeTypeIndexEntry& entry() const override;
    void next() override;

private:
    void readCurrent();

    WT_CURSOR* cursor_ = nullptr;
    WT_SESSION* session_ = nullptr;
    std::string prefix_;
    bool valid_ = false;
    ISyncGraphDataStore::EdgeTypeIndexEntry current_;
    std::string currentKey_;
    std::string currentValue_;
};

} // namespace eugraph
