# 接口设计

> [当前实现] 参见 [README.md](../README.md) 返回文档导航

## 架构原则

存储层采用 **sync/async 分离** 设计：

- **Sync 接口**：直接封装 WiredTiger 调用，同步阻塞。仅在 IO 线程上使用。
- **Async 接口**：通过 `IoScheduler` 将 sync 调用调度到 IO 线程池，暴露协程接口。计算层和服务层只依赖 async 接口。

```
                    ┌──────────────────────────────┐
                    │  Compute Layer / Server Layer │
                    │  (只依赖 async 接口)            │
                    └──────────────┬───────────────┘
                                   │ co_await
                    ┌──────────────▼───────────────┐
                    │    IoScheduler (IO 线程池)     │
                    └──────────────┬───────────────┘
                                   │ dispatch
                    ┌──────────────▼───────────────┐
                    │    Sync Layer (WiredTiger)     │
                    └──────────────────────────────┘
```

## 数据存储接口

### ISyncGraphDataStore

文件：`src/storage/data/i_sync_graph_data_store.hpp`

```cpp
class ISyncGraphDataStore {
public:
    // 生命周期
    virtual bool open(const std::string& db_path) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    // 事务
    virtual GraphTxnHandle beginTransaction() = 0;
    virtual bool commitTransaction(GraphTxnHandle txn) = 0;
    virtual bool rollbackTransaction(GraphTxnHandle txn) = 0;

    // DDL — 创建/删除物理表
    virtual bool createLabel(LabelId label_id) = 0;
    virtual bool dropLabel(LabelId label_id) = 0;
    virtual bool createEdgeLabel(EdgeLabelId edge_label_id) = 0;
    virtual bool dropEdgeLabel(EdgeLabelId edge_label_id) = 0;

    // Vertex CRUD
    virtual bool insertVertex(GraphTxnHandle txn, VertexId vid,
                              std::span<const std::pair<LabelId, Properties>> label_props,
                              const PropertyValue* pk_value) = 0;
    virtual bool deleteVertex(GraphTxnHandle txn, VertexId vid) = 0;

    // Vertex Properties
    virtual std::optional<Properties> getVertexProperties(GraphTxnHandle txn, VertexId vid, LabelId label_id) = 0;
    virtual std::optional<PropertyValue> getVertexProperty(GraphTxnHandle txn, VertexId vid, LabelId label_id, uint16_t prop_id) = 0;
    virtual bool putVertexProperty(GraphTxnHandle txn, VertexId vid, LabelId label_id, uint16_t prop_id, const PropertyValue& value) = 0;
    virtual bool putVertexProperties(GraphTxnHandle txn, VertexId vid, LabelId label_id, const Properties& props) = 0;
    virtual bool deleteVertexProperty(GraphTxnHandle txn, VertexId vid, LabelId label_id, uint16_t prop_id) = 0;

    // Vertex Labels
    virtual LabelIdSet getVertexLabels(GraphTxnHandle txn, VertexId vid) = 0;
    virtual bool addVertexLabel(GraphTxnHandle txn, VertexId vid, LabelId label_id) = 0;
    virtual bool removeVertexLabel(GraphTxnHandle txn, VertexId vid, LabelId label_id) = 0;

    // Primary Key
    virtual std::optional<VertexId> getVertexIdByPk(const PropertyValue& pk_value) = 0;
    virtual std::optional<PropertyValue> getPkByVertexId(GraphTxnHandle txn, VertexId vid) = 0;

    // Vertex Scan
    virtual void scanVerticesByLabel(GraphTxnHandle txn, LabelId label_id,
                                     std::function<void(VertexId)> callback) = 0;
    virtual std::unique_ptr<IVertexScanCursor> createVertexScanCursor(GraphTxnHandle txn, LabelId label_id) = 0;

    // Edge CRUD
    virtual bool insertEdge(GraphTxnHandle txn, EdgeId eid, VertexId src_id, VertexId dst_id,
                            EdgeLabelId label_id, uint64_t seq, const Properties& props) = 0;
    virtual bool deleteEdge(GraphTxnHandle txn, EdgeId eid, EdgeLabelId label_id,
                            VertexId src_id, VertexId dst_id, uint64_t seq) = 0;

    // Edge Properties
    virtual std::optional<Properties> getEdgeProperties(GraphTxnHandle txn, EdgeId eid, EdgeLabelId label_id) = 0;
    virtual std::optional<PropertyValue> getEdgeProperty(GraphTxnHandle txn, EdgeId eid, EdgeLabelId label_id, uint16_t prop_id) = 0;
    virtual bool putEdgeProperty(GraphTxnHandle txn, EdgeId eid, EdgeLabelId label_id, uint16_t prop_id, const PropertyValue& value) = 0;
    virtual bool deleteEdgeProperty(GraphTxnHandle txn, EdgeId eid, EdgeLabelId label_id, uint16_t prop_id) = 0;

    // Edge Traversal
    virtual void scanEdges(GraphTxnHandle txn, VertexId vid, Direction direction,
                           std::optional<EdgeLabelId> label_filter,
                           std::function<void(const EdgeIndexEntry&)> callback) = 0;
    virtual std::unique_ptr<IEdgeScanCursor> createEdgeScanCursor(GraphTxnHandle txn, VertexId vid,
                                                                   Direction direction,
                                                                   std::optional<EdgeLabelId> label_filter) = 0;
    virtual void scanEdgesByType(GraphTxnHandle txn, EdgeLabelId label_id,
                                 std::optional<VertexId> src_filter, std::optional<VertexId> dst_filter,
                                 std::function<void(const EdgeTypeIndexEntry&)> callback) = 0;
    virtual std::unique_ptr<IEdgeTypeScanCursor> createEdgeTypeScanCursor(GraphTxnHandle txn, EdgeLabelId label_id,
                                                                          std::optional<VertexId> src_filter,
                                                                          std::optional<VertexId> dst_filter) = 0;

    // Statistics
    virtual uint64_t countVerticesByLabel(GraphTxnHandle txn, LabelId label_id) = 0;
    virtual uint64_t countDegree(GraphTxnHandle txn, VertexId vid, Direction direction,
                                 std::optional<EdgeLabelId> label_filter) = 0;

    // Index DDL
    virtual bool createIndex(const std::string& table_name) = 0;
    virtual bool dropIndex(const std::string& table_name) = 0;

    // Index Entries
    virtual bool insertIndexEntry(const std::string& table, const PropertyValue& value, uint64_t entity_id) = 0;
    virtual bool deleteIndexEntry(const std::string& table, const PropertyValue& value, uint64_t entity_id) = 0;
    virtual bool checkUniqueConstraint(const std::string& table, const PropertyValue& value) = 0;

    // Index Scan
    virtual void scanIndexEquality(GraphTxnHandle txn, const std::string& table, const PropertyValue& value,
                                   std::function<void(uint64_t entity_id)> callback) = 0;
    virtual void scanIndexRange(GraphTxnHandle txn, const std::string& table,
                                std::optional<PropertyValue> start, std::optional<PropertyValue> end,
                                std::function<void(uint64_t entity_id)> callback) = 0;

    // Index Cleanup
    virtual bool dropAllIndexEntries(const std::string& table) = 0;
};
```

### IAsyncGraphDataStore

文件：`src/storage/data/i_async_graph_data_store.hpp`

```cpp
class IAsyncGraphDataStore {
public:
    // 事务
    virtual folly::coro::Task<GraphTxnHandle> beginTran() = 0;
    virtual folly::coro::Task<bool> commitTran(GraphTxnHandle txn) = 0;
    virtual folly::coro::Task<bool> rollbackTran(GraphTxnHandle txn) = 0;
    virtual void setTransaction(GraphTxnHandle txn) = 0;

    // DDL
    virtual folly::coro::Task<bool> createLabel(LabelId label_id) = 0;
    virtual folly::coro::Task<bool> createEdgeLabel(EdgeLabelId edge_label_id) = 0;

    // Vertex Read
    virtual folly::coro::Task<std::optional<Properties>> getVertexProperties(VertexId vid, LabelId label_id) = 0;
    virtual folly::coro::Task<std::optional<PropertyValue>> getVertexProperty(VertexId vid, LabelId label_id, uint16_t prop_id) = 0;
    virtual folly::coro::Task<LabelIdSet> getVertexLabels(VertexId vid) = 0;

    // Vertex Write
    virtual folly::coro::Task<bool> insertVertex(VertexId vid,
                                                  std::span<const std::pair<LabelId, Properties>> label_props,
                                                  const PropertyValue* pk_value) = 0;
    virtual folly::coro::Task<bool> deleteVertex(VertexId vid) = 0;

    // Edge Write
    virtual folly::coro::Task<bool> insertEdge(EdgeId eid, VertexId src_id, VertexId dst_id,
                                               EdgeLabelId label_id, uint64_t seq, const Properties& props) = 0;
    virtual folly::coro::Task<bool> deleteEdge(EdgeId eid, EdgeLabelId label_id,
                                               VertexId src_id, VertexId dst_id, uint64_t seq) = 0;

    // Scan (AsyncGenerator)
    virtual folly::coro::AsyncGenerator<std::vector<VertexId>> scanVerticesByLabel(LabelId label_id) = 0;
    virtual folly::coro::AsyncGenerator<std::vector<EdgeIndexEntry>> scanEdges(
        VertexId vid, Direction direction, std::optional<EdgeLabelId> label_filter) = 0;
    virtual folly::coro::AsyncGenerator<std::vector<EdgeTypeIndexEntry>> scanEdgesByType(
        EdgeLabelId label_id, std::optional<VertexId> src_filter, std::optional<VertexId> dst_filter) = 0;

    // Index
    virtual folly::coro::Task<bool> createIndex(const std::string& table_name) = 0;
    virtual folly::coro::Task<bool> dropIndex(const std::string& table_name) = 0;
    virtual folly::coro::Task<bool> insertIndexEntry(const std::string& table, const PropertyValue& value, uint64_t entity_id) = 0;
    virtual folly::coro::Task<bool> deleteIndexEntry(const std::string& table, const PropertyValue& value, uint64_t entity_id) = 0;

    // Index Scan
    virtual folly::coro::AsyncGenerator<std::vector<VertexId>> scanVerticesByIndex(
        LabelId label_id, uint16_t prop_id, const PropertyValue& value) = 0;
    virtual folly::coro::AsyncGenerator<std::vector<VertexId>> scanVerticesByIndexRange(
        LabelId label_id, uint16_t prop_id,
        std::optional<PropertyValue> start, std::optional<PropertyValue> end) = 0;

    // Batch Write (供 loader 使用，独立事务)
    virtual folly::coro::Task<std::vector<VertexId>> batchInsertVertices(
        LabelId label_id, const std::vector<BatchVertexEntry>& entries) = 0;
    virtual folly::coro::Task<int32_t> batchInsertEdges(
        EdgeLabelId edge_label_id, const std::vector<BatchEdgeEntry>& entries) = 0;
};
```

## 元数据存储接口

### ISyncGraphMetaStore

文件：`src/storage/meta/i_sync_graph_meta_store.hpp`

```cpp
class ISyncGraphMetaStore {
public:
    // 生命周期 (独立 WT 连接在 {db}/meta/)
    virtual bool open(const std::string& db_path) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    // 事务
    virtual GraphTxnHandle beginTransaction() = 0;
    virtual bool commitTransaction(GraphTxnHandle txn) = 0;
    virtual bool rollbackTransaction(GraphTxnHandle txn) = 0;

    // 元数据 KV
    virtual bool metadataPut(std::string_view key, std::string_view value) = 0;
    virtual std::optional<std::string> metadataGet(std::string_view key) = 0;
    virtual bool metadataDel(std::string_view key) = 0;
    virtual void metadataScan(std::string_view prefix,
                              std::function<void(std::string_view key, std::string_view value)> callback) = 0;

    // DDL (仅操作元数据 KV，不创建物理表 — 均为 no-op)
    virtual bool createLabel(LabelId label_id) = 0;
    virtual bool dropLabel(LabelId label_id) = 0;
    virtual bool createEdgeLabel(EdgeLabelId edge_label_id) = 0;
    virtual bool dropEdgeLabel(EdgeLabelId edge_label_id) = 0;

    // Durability
    virtual bool checkpoint() = 0;
};
```

### IAsyncGraphMetaStore

文件：`src/storage/meta/i_async_graph_meta_store.hpp`

```cpp
class IAsyncGraphMetaStore {
public:
    // 生命周期
    virtual folly::coro::Task<bool> open(ISyncGraphMetaStore& store, IoScheduler& io) = 0;
    virtual folly::coro::Task<void> close() = 0;

    // Label 管理
    virtual folly::coro::Task<LabelId> createLabel(const std::string& name,
                                                   const std::vector<PropertyDef>& properties = {}) = 0;
    virtual folly::coro::Task<std::optional<LabelId>> getLabelId(const std::string& name) = 0;
    virtual folly::coro::Task<std::optional<std::string>> getLabelName(LabelId id) = 0;
    virtual folly::coro::Task<std::optional<LabelDef>> getLabelDef(const std::string& name) = 0;
    virtual folly::coro::Task<std::optional<LabelDef>> getLabelDefById(LabelId id) = 0;
    virtual folly::coro::Task<std::vector<LabelDef>> listLabels() = 0;

    // EdgeLabel 管理
    virtual folly::coro::Task<EdgeLabelId> createEdgeLabel(const std::string& name,
                                                           const std::vector<PropertyDef>& properties = {},
                                                           bool directed = true) = 0;
    virtual folly::coro::Task<std::optional<EdgeLabelId>> getEdgeLabelId(const std::string& name) = 0;
    virtual folly::coro::Task<std::optional<std::string>> getEdgeLabelName(EdgeLabelId id) = 0;
    virtual folly::coro::Task<std::optional<EdgeLabelDef>> getEdgeLabelDef(const std::string& name) = 0;
    virtual folly::coro::Task<std::optional<EdgeLabelDef>> getEdgeLabelDefById(EdgeLabelId id) = 0;
    virtual folly::coro::Task<std::vector<EdgeLabelDef>> listEdgeLabels() = 0;

    // ID 分配
    virtual folly::coro::Task<VertexId> nextVertexId() = 0;
    virtual folly::coro::Task<EdgeId> nextEdgeId() = 0;
    virtual folly::coro::Task<std::vector<VertexId>> nextVertexIdRange(size_t count) = 0;
    virtual folly::coro::Task<std::vector<EdgeId>> nextEdgeIdRange(size_t count) = 0;

    // 索引管理
    virtual folly::coro::Task<bool> createVertexIndex(const std::string& name, const std::string& label_name,
                                                      const std::string& prop_name, bool unique) = 0;
    virtual folly::coro::Task<bool> createEdgeIndex(const std::string& name, const std::string& edge_label_name,
                                                    const std::string& prop_name, bool unique) = 0;
    virtual folly::coro::Task<bool> updateIndexState(const std::string& name, IndexState new_state) = 0;
    virtual folly::coro::Task<bool> dropIndex(const std::string& name) = 0;
    virtual folly::coro::Task<std::vector<IndexInfo>> listIndexes() = 0;
    virtual folly::coro::Task<std::optional<IndexInfo>> getIndex(const std::string& name) = 0;

    // Schema 访问
    virtual const GraphSchema& schema() const = 0;
};
```

## 关键设计决策

1. **独立 WT 连接**：meta store 和 data store 各自拥有独立的 WiredTiger 连接，分别存储在 `{db}/meta/` 和 `{db}/data/`。
2. **compute 层零 sync 依赖**：`QueryExecutor` 和 `PhysicalOperator` 只依赖 `IAsyncGraphDataStore` 和 `IAsyncGraphMetaStore`。
3. **事务通过 async 接口**：`beginTran/commitTran/rollbackTran` 是 async 方法，内部 dispatch 到 IO 线程池。
4. **DDL 协调在 handler 层**：handler 先调 `async_meta_.createLabel()` 持久化元数据，再调 `async_data_.createLabel()` 创建物理表。
5. **ISyncGraphMetaStore DDL 方法为 no-op**：元数据只存在自己的 `table:metadata` 中；物理表的创建由 data store 负责。
6. **批量操作使用独立事务**：`batchInsertVertices`/`batchInsertEdges` 内部自行 begin+commit，不参与外层 Cypher 事务。
