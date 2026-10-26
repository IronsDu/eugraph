#pragma once

#include "common/types/constants.hpp"
#include "storage/data/cursor_provider.hpp"
#include "storage/data/i_sync_graph_data_store.hpp"
#include "storage/kv/key_codec.hpp"
#include "storage/kv/value_codec.hpp"
#include "storage/schema_ddl.hpp"
#include "storage/wt_connection_store.hpp"
#include "storage/wt_cursor.hpp"

#include <cassert>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

namespace eugraph {

/// ISyncGraphDataStore implementation using WiredTiger multi-table storage.
///
/// **纯读写**（docs/storage/session-cursor-pool-design.md §2）：
///  - 连接由 `WtConnectionStore` 拥有 ✓（本类只 bind ✓）
///  - 事务由 `GraphSession` 拥有 ✓（本类只接收 `GraphSession&`，不建 session、不开/提交事务 ✗）
///  - DDL（create/drop table）是管理类操作 ⇒ 单次 admin session ✓
class SyncGraphDataStore : public ISyncGraphDataStore {
public:
    SyncGraphDataStore() = default;
    ~SyncGraphDataStore() override = default;

    SyncGraphDataStore(const SyncGraphDataStore&) = delete;
    SyncGraphDataStore& operator=(const SyncGraphDataStore&) = delete;

    /// 初始化期绑定连接（连接归 WtConnectionStore ✓）
    void bind_connection(WtConnectionStore& conn) noexcept {
        conn_ = &conn;
    }
    [[nodiscard]] bool isOpen() const noexcept {
        return conn_ != nullptr && conn_->is_open();
    }
    [[nodiscard]] WtConnectionStore& conn() noexcept {
        assert(conn_ != nullptr);
        return *conn_;
    }
    [[nodiscard]] const WtConnectionStore& conn() const noexcept {
        assert(conn_ != nullptr);
        return *conn_;
    }

    /// 建表（幂等 ✓）：全局表由 GraphManager 在 bind 之后调用 ✓（session 由调用方创建/拥有）
    bool ensure_tables();

    // DDL（session 由调用方创建/拥有；DDL 必须在非事务会话上执行）
    bool createLabel(LabelId label_id) override;
    bool dropLabel(LabelId label_id) override;
    bool createEdgeLabel(EdgeLabelId edge_label_id) override;
    bool dropEdgeLabel(EdgeLabelId edge_label_id) override;

    // Vertex
    bool insertVertex(GraphSession& session, VertexId vid,
                      std::span<const std::pair<LabelId, Properties>> label_props) override;
    bool deleteVertex(GraphSession& session, VertexId vid) override;

    // Vertex Properties
    std::optional<Properties> getVertexProperties(GraphSession& session, VertexId vid, LabelId label_id) override;
    /// One cursor covers the whole batch: `openCursor` once, then a point `search` per
    /// (row, property). Measured (see docs/storage/session-cursor-ownership.md, POC-1):
    /// cursor open/close is 51.6% of a point lookup, so paying it once per batch instead of
    /// once per lookup is the single biggest per-row saving available in the storage layer.
    std::vector<std::optional<Properties>>
    getVertexPropertiesBatchProjected(GraphSession& session, LabelId label_id, const std::vector<VertexId>& vids,
                                      const std::vector<uint16_t>& proj) override;
    std::optional<PropertyValue> getVertexProperty(GraphSession& session, VertexId vid, LabelId label_id,
                                                   uint16_t prop_id) override;
    bool putVertexProperty(GraphSession& session, VertexId vid, LabelId label_id, uint16_t prop_id,
                           const PropertyValue& value) override;
    bool putVertexProperties(GraphSession& session, VertexId vid, LabelId label_id, const Properties& props) override;
    bool deleteVertexProperty(GraphSession& session, VertexId vid, LabelId label_id, uint16_t prop_id) override;

    // Vertex Labels
    LabelIdSet getVertexLabels(GraphSession& session, VertexId vid) override;
    std::vector<LabelIdSet> getVertexLabelsBatch(GraphSession& session, const std::vector<VertexId>& vids) override;
    std::vector<std::optional<Properties>> getVertexPropertiesBatch(GraphSession& session, LabelId label_id,
                                                                    const std::vector<VertexId>& vids) override;
    std::vector<std::vector<PropertyValue>>
    getVertexCoalescePropertiesBatch(GraphSession& session, const std::vector<VertexId>& vids,
                                     const std::vector<std::pair<LabelId, uint16_t>>& candidates) override;
    bool addVertexLabel(GraphSession& session, VertexId vid, LabelId label_id) override;
    bool removeVertexLabel(GraphSession& session, VertexId vid, LabelId label_id) override;

    // Label Index Scan
    void scanVerticesByLabel(GraphSession& session, LabelId label_id,
                             const std::function<bool(VertexId)>& callback) override;
    std::unique_ptr<IVertexScanCursor> createVertexScanCursor(GraphSession& session, LabelId label_id) override;
    std::unique_ptr<IVertexScanCursor> createAllVertexScanCursor(GraphSession& session) override;

    // All-Vertex Scan (含无标签 vertex)
    void scanAllVertices(GraphSession& session, const std::function<bool(VertexId)>& callback) override;

    // Edge
    bool insertEdge(GraphSession& session, EdgeId eid, VertexId src_id, VertexId dst_id, EdgeLabelId label_id,
                    uint64_t seq, const Properties& props) override;
    bool deleteEdge(GraphSession& session, EdgeId eid, EdgeLabelId label_id, VertexId src_id, VertexId dst_id,
                    uint64_t seq) override;

    // Edge Properties
    std::optional<Properties> getEdgeProperties(GraphSession& session, EdgeLabelId label_id, EdgeId eid) override;
    std::optional<PropertyValue> getEdgeProperty(GraphSession& session, EdgeLabelId label_id, EdgeId eid,
                                                 uint16_t prop_id) override;
    std::vector<std::optional<PropertyValue>> getEdgePropertyBatch(GraphSession& session, EdgeLabelId label_id,
                                                                   const std::vector<EdgeId>& edge_ids,
                                                                   uint16_t prop_id) override;
    bool putEdgeProperty(GraphSession& session, EdgeLabelId label_id, EdgeId eid, uint16_t prop_id,
                         const PropertyValue& value) override;
    bool deleteEdgeProperty(GraphSession& session, EdgeLabelId label_id, EdgeId eid, uint16_t prop_id) override;

    // Edge Traversal
    void scanEdges(GraphSession& session, VertexId vid, Direction direction, std::optional<EdgeLabelId> label_filter,
                   const std::function<bool(const EdgeIndexEntry&)>& callback) override;
    void scanEdgesBatch(GraphSession& session, const std::vector<VertexId>& src_ids, Direction direction,
                        std::optional<EdgeLabelId> label_filter,
                        const std::function<bool(VertexId, const EdgeIndexEntry&)>& callback) override;
    std::unique_ptr<IEdgeScanCursor> createEdgeScanCursor(GraphSession& session, VertexId vid, Direction direction,
                                                          std::optional<EdgeLabelId> label_filter) override;
    void scanEdgesByType(GraphSession& session, EdgeLabelId label_id, std::optional<VertexId> src_filter,
                         std::optional<VertexId> dst_filter,
                         const std::function<bool(const EdgeTypeIndexEntry&)>& callback) override;
    std::unique_ptr<IEdgeTypeScanCursor> createEdgeTypeScanCursor(GraphSession& session, EdgeLabelId label_id,
                                                                  std::optional<VertexId> src_filter,
                                                                  std::optional<VertexId> dst_filter) override;

    // Statistics
    uint64_t countVerticesByLabel(GraphSession& session, LabelId label_id) override;
    uint64_t countDegree(GraphSession& session, VertexId vid, Direction direction,
                         std::optional<EdgeLabelId> label_filter) override;

    // Index DDL
    bool createIndex(const std::string& table_name) override;
    bool dropIndex(const std::string& table_name) override;

    // Index Entry Operations
    bool insertIndexEntry(GraphSession& session, const std::string& table, const PropertyValue& value,
                          uint64_t entity_id) override;
    bool insertIndexEntry(GraphSession& session, const std::string& table, const std::vector<PropertyValue>& values,
                          uint64_t entity_id) override;
    bool insertIndexEntry(GraphSession& session, const std::string& table, const PropertyValue& value,
                          uint64_t entity_id, std::string_view payload) override;
    bool insertIndexEntry(GraphSession& session, const std::string& table, const std::vector<PropertyValue>& values,
                          uint64_t entity_id, std::string_view payload) override;
    bool deleteIndexEntry(GraphSession& session, const std::string& table, const PropertyValue& value,
                          uint64_t entity_id) override;
    bool deleteIndexEntry(GraphSession& session, const std::string& table, const std::vector<PropertyValue>& values,
                          uint64_t entity_id) override;
    bool checkUniqueConstraint(GraphSession& session, const std::string& table, const PropertyValue& value) override;
    bool checkUniqueConstraint(GraphSession& session, const std::string& table,
                               const std::vector<PropertyValue>& values) override;

    // Index Scan
    void scanIndexEquality(GraphSession& session, const std::string& table, const PropertyValue& value,
                           const std::function<bool(uint64_t)>& callback) override;
    void scanIndexEquality(GraphSession& session, const std::string& table, const std::vector<PropertyValue>& values,
                           const std::function<bool(uint64_t)>& callback) override;
    void scanIndexRange(GraphSession& session, const std::string& table, const std::optional<PropertyValue>& start,
                        const std::optional<PropertyValue>& end,
                        const std::function<bool(uint64_t)>& callback) override;
    void scanIndexRange(GraphSession& session, const std::string& table,
                        const std::optional<std::vector<PropertyValue>>& start,
                        const std::optional<std::vector<PropertyValue>>& end,
                        const std::function<bool(uint64_t)>& callback) override;
    void scanIndexEqualityWithValue(GraphSession& session, const std::string& table, const PropertyValue& value,
                                    const std::function<bool(uint64_t, std::string_view)>& callback) override;
    void scanIndexEqualityWithValue(GraphSession& session, const std::string& table,
                                    const std::vector<PropertyValue>& values,
                                    const std::function<bool(uint64_t, std::string_view)>& callback) override;
    void scanIndexRangeWithValue(GraphSession& session, const std::string& table,
                                 const std::optional<PropertyValue>& start, const std::optional<PropertyValue>& end,
                                 const std::function<bool(uint64_t, std::string_view)>& callback) override;
    void scanIndexRangeWithValue(GraphSession& session, const std::string& table,
                                 const std::optional<std::vector<PropertyValue>>& start,
                                 const std::optional<std::vector<PropertyValue>>& end,
                                 const std::function<bool(uint64_t, std::string_view)>& callback) override;

    // 可恢复的索引扫描 cursor（async 层流式消费 ✓）
    std::unique_ptr<IIndexScanCursor> createIndexEqualityCursor(GraphSession& session, const std::string& table,
                                                                const PropertyValue& value) override;
    std::unique_ptr<IIndexScanCursor> createIndexEqualityCursor(GraphSession& session, const std::string& table,
                                                                const std::vector<PropertyValue>& values) override;
    std::unique_ptr<IIndexScanCursor> createIndexRangeCursor(GraphSession& session, const std::string& table,
                                                             const std::optional<PropertyValue>& start,
                                                             const std::optional<PropertyValue>& end) override;
    std::unique_ptr<IIndexScanCursor>
    createIndexRangeCursor(GraphSession& session, const std::string& table,
                           const std::optional<std::vector<PropertyValue>>& start,
                           const std::optional<std::vector<PropertyValue>>& end) override;

private:
    // ==================== vertex_data adjacency helpers ====================
    struct AdjState {
        KeyCodec::AdjacencyCacheState state = KeyCodec::AdjacencyCacheState::OVERFLOW;
        uint32_t count = 0;
    };

    std::optional<AdjState> getAdjacencyState(WT_SESSION* session, VertexId vid, Direction direction);
    bool putAdjacencyState(WT_SESSION* session, VertexId vid, Direction direction, KeyCodec::AdjacencyCacheState state,
                           uint32_t count);
    bool insertAdjacencyEdge(WT_SESSION* session, VertexId vid, Direction direction, EdgeLabelId label_id,
                             VertexId neighbor_id, uint64_t seq, EdgeId eid);
    bool deleteAdjacencyEdge(WT_SESSION* session, VertexId vid, Direction direction, EdgeLabelId label_id,
                             VertexId neighbor_id, uint64_t seq, EdgeId eid);
    bool promoteAdjacency(WT_SESSION* session, VertexId vid, Direction direction, uint32_t cached_count);
    bool collectCachedEdges(WT_SESSION* session, VertexId vid, Direction direction,
                            std::optional<EdgeLabelId> label_filter,
                            std::vector<ISyncGraphDataStore::EdgeIndexEntry>& out);

    WtConnectionStore* conn_ = nullptr;
    SchemaDdl schema_ddl_;
};

// ==================== Scan Cursor Implementations ====================

class VertexScanCursorImpl : public ISyncGraphDataStore::IVertexScanCursor {
public:
    /// cursor 由调用方从 `GraphSession::acquire_scan_cursor()` 取得并移交所有权 ✓
    VertexScanCursorImpl(GraphSession& session, CursorHandle cursor, bool existence_keys = false);
    ~VertexScanCursorImpl() override;

    bool valid() const override;
    VertexId vertexId() const override;
    void next() override;

private:
    void readCurrent();

    GraphSession& session_; // 仅用于把迭代错误粘到事务 ✓（cursor 生命周期 ⊆ 会话 ✓）
    CursorHandle cursor_;
    bool valid_ = false;
    VertexId current_vid_ = 0;
    // label 前向表用 label-forward key，顶点存在表用 vertex-existence key。
    bool existence_keys_ = false;
};

class EdgeScanCursorImpl : public ISyncGraphDataStore::IEdgeScanCursor {
public:
    EdgeScanCursorImpl(GraphSession& session, CursorHandle cursor, std::string_view prefix);
    ~EdgeScanCursorImpl() override;

    bool valid() const override;
    const ISyncGraphDataStore::EdgeIndexEntry& entry() const override;
    void next() override;

private:
    void readCurrent();

    GraphSession& session_;
    CursorHandle cursor_;
    std::string prefix_;
    bool valid_ = false;
    ISyncGraphDataStore::EdgeIndexEntry current_;
};

/// Streaming cursor over cached (COMPLETE) adjacency records in vertex_data.
/// Positions after the direction's state key and skips it on advances.
class VertexDataEdgeScanCursor : public ISyncGraphDataStore::IEdgeScanCursor {
public:
    VertexDataEdgeScanCursor(GraphSession& session, CursorHandle cursor, std::string_view prefix,
                             std::optional<EdgeLabelId> label_filter);
    ~VertexDataEdgeScanCursor() override = default;

    bool valid() const override {
        return valid_;
    }
    const ISyncGraphDataStore::EdgeIndexEntry& entry() const override {
        return current_;
    }
    void next() override;
    bool cacheUsable() const {
        return cache_usable_;
    }

private:
    void readCurrent();

    GraphSession& session_;
    CursorHandle cursor_;
    std::string prefix_;
    std::optional<EdgeLabelId> label_filter_;
    bool valid_ = false;
    bool cache_usable_ = false;
    ISyncGraphDataStore::EdgeIndexEntry current_{};
};

/// Materialized edge cursor for cached (COMPLETE) adjacency; also used for BOTH.
class VectorEdgeScanCursor : public ISyncGraphDataStore::IEdgeScanCursor {
public:
    explicit VectorEdgeScanCursor(std::vector<ISyncGraphDataStore::EdgeIndexEntry> entries)
        : entries_(std::move(entries)) {}
    bool valid() const override {
        return pos_ < entries_.size();
    }
    const ISyncGraphDataStore::EdgeIndexEntry& entry() const override {
        return entries_[pos_];
    }
    void next() override {
        ++pos_;
    }

private:
    std::vector<ISyncGraphDataStore::EdgeIndexEntry> entries_;
    size_t pos_ = 0;
};

class EdgeTypeScanCursorImpl : public ISyncGraphDataStore::IEdgeTypeScanCursor {
public:
    EdgeTypeScanCursorImpl(GraphSession& session, CursorHandle cursor, std::string_view prefix);
    ~EdgeTypeScanCursorImpl() override;

    bool valid() const override;
    const ISyncGraphDataStore::EdgeTypeIndexEntry& entry() const override;
    void next() override;

private:
    void readCurrent();

    GraphSession& session_;
    CursorHandle cursor_;
    std::string prefix_;
    bool valid_ = false;
    ISyncGraphDataStore::EdgeTypeIndexEntry current_;
};

// ==================== Index Scan Cursor ====================

/// 可恢复的索引扫描 cursor：equality（prefix）或 range（[start, end)）✓
class IndexScanCursorImpl : public ISyncGraphDataStore::IIndexScanCursor {
public:
    struct Spec {
        std::string start_key; // equality: prefix；range: 已编码下界（空 = 从头）
        std::string end_key;   // range 上界（已编码、不含 entity_id 后缀；空 = 无界）
        bool exclusive_start = false;
        bool prefix_mode = false; // true ⇒ equality：key 必须以 start_key 开头
    };

    IndexScanCursorImpl(GraphSession& session, CursorHandle cursor, Spec spec);
    ~IndexScanCursorImpl() override;

    bool valid() const override;
    uint64_t entityId() const override;
    std::string_view value() const override;
    void next() override;

private:
    void position();
    void readCurrent();
    void fail(int wt_error, const char* what);

    GraphSession& session_;
    CursorHandle cursor_;
    Spec spec_;
    bool valid_ = false;
    uint64_t entity_id_ = 0;
    std::string value_;
};

} // namespace eugraph
