# 接口设计

> 参见 [overview.md](overview.md) 返回文档导航

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

同步图数据存储接口，直接操作 WiredTiger。

```cpp
class ISyncGraphDataStore {
public:
    // 生命周期
    virtual bool open(const std::string& db_path) = 0;
    virtual void close() = 0;

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
    virtual bool putVertexProperties(GraphTxnHandle txn, VertexId vid, LabelId label_id, const Properties& props) = 0;

    // Vertex Labels
    virtual LabelIdSet getVertexLabels(GraphTxnHandle txn, VertexId vid) = 0;

    // Label Index Scan (cursor-based)
    virtual std::unique_ptr<IVertexScanCursor> createVertexScanCursor(GraphTxnHandle txn, LabelId label_id) = 0;

    // Edge CRUD
    virtual bool insertEdge(GraphTxnHandle txn, EdgeId eid, VertexId src_id, VertexId dst_id,
                            EdgeLabelId label_id, uint64_t seq, const Properties& props) = 0;

    // Edge Traversal (cursor-based)
    virtual std::unique_ptr<IEdgeScanCursor> createEdgeScanCursor(GraphTxnHandle txn, VertexId vid,
                                                                   Direction direction,
                                                                   std::optional<EdgeLabelId> label_filter) = 0;
    virtual std::unique_ptr<IEdgeTypeScanCursor> createEdgeTypeScanCursor(GraphTxnHandle txn, EdgeLabelId label_id,
                                                                          std::optional<VertexId> src_filter,
                                                                          std::optional<VertexId> dst_filter) = 0;

    // Statistics
    virtual uint64_t countVerticesByLabel(GraphTxnHandle txn, LabelId label_id) = 0;
    virtual uint64_t countDegree(GraphTxnHandle txn, VertexId vid, Direction direction,
                                 std::optional<EdgeLabelId> label_filter) = 0;
};
```

### IAsyncGraphDataStore

文件：`src/storage/data/i_async_graph_data_store.hpp`

异步图数据存储接口。所有方法返回协程类型，通过 IoScheduler 调度到 IO 线程池。

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

    // Vertex
    virtual folly::coro::Task<std::optional<Properties>> getVertexProperties(VertexId vid, LabelId label_id) = 0;
    virtual folly::coro::Task<LabelIdSet> getVertexLabels(VertexId vid) = 0;

    // Scan (批量 AsyncGenerator)
    virtual folly::coro::AsyncGenerator<std::vector<VertexId>> scanVerticesByLabel(LabelId label_id) = 0;
    virtual folly::coro::AsyncGenerator<std::vector<EdgeIndexEntry>> scanEdges(...) = 0;
    virtual folly::coro::AsyncGenerator<std::vector<EdgeTypeIndexEntry>> scanEdgesByType(...) = 0;

    // Write
    virtual folly::coro::Task<bool> insertVertex(VertexId vid,
                                                  std::span<const std::pair<LabelId, Properties>> label_props,
                                                  const PropertyValue* pk_value) = 0;
    virtual folly::coro::Task<bool> insertEdge(EdgeId eid, VertexId src_id, VertexId dst_id,
                                               EdgeLabelId label_id, uint64_t seq, const Properties& props) = 0;
};
```

## 元数据存储接口

### ISyncGraphMetaStore

文件：`src/storage/meta/i_sync_graph_meta_store.hpp`

同步元数据 KV 操作接口。

```cpp
class ISyncGraphMetaStore {
public:
    // 生命周期 (独立 WT 连接在 {db}/meta/)
    virtual bool open(const std::string& db_path) = 0;
    virtual void close() = 0;

    // 事务
    virtual GraphTxnHandle beginTransaction() = 0;
    virtual bool commitTransaction(GraphTxnHandle txn) = 0;
    virtual bool rollbackTransaction(GraphTxnHandle txn) = 0;

    // 元数据 KV
    virtual bool metadataPut(std::string_view key, std::string_view value) = 0;
    virtual std::optional<std::string> metadataGet(std::string_view key) = 0;
    virtual void metadataScan(std::string_view prefix, ...) = 0;

    // DDL (仅操作元数据 KV，不创建物理表)
    virtual bool createLabel(LabelId label_id) = 0;
    virtual bool dropLabel(LabelId label_id) = 0;
    virtual bool createEdgeLabel(EdgeLabelId edge_label_id) = 0;
    virtual bool dropEdgeLabel(EdgeLabelId edge_label_id) = 0;
};
```

### IAsyncGraphMetaStore

文件：`src/storage/meta/i_async_graph_meta_store.hpp`

异步元数据服务接口。维护内存 `GraphSchema`，所有阻塞调用通过 IoScheduler 调度。

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
    virtual folly::coro::Task<std::optional<LabelDef>> getLabelDef(const std::string& name) = 0;
    virtual folly::coro::Task<std::vector<LabelDef>> listLabels() = 0;

    // EdgeLabel 管理
    virtual folly::coro::Task<EdgeLabelId> createEdgeLabel(const std::string& name, ...) = 0;
    virtual folly::coro::Task<std::vector<EdgeLabelDef>> listEdgeLabels() = 0;

    // ID 分配
    virtual folly::coro::Task<VertexId> nextVertexId() = 0;
    virtual folly::coro::Task<EdgeId> nextEdgeId() = 0;

    // Schema 访问
    virtual const GraphSchema& schema() const = 0;
};
```

## 关键设计决策

1. **独立 WT 连接**：meta store 和 data store 各自拥有独立的 WiredTiger 连接，分别存储在 `{db}/meta/` 和 `{db}/data/`。
2. **compute 层零 sync 依赖**：`QueryExecutor` 和 `PhysicalOperator` 只依赖 `IAsyncGraphDataStore` 和 `IAsyncGraphMetaStore`。
3. **事务通过 async 接口**：`beginTran/commitTran/rollbackTran` 是 async 方法，内部 dispatch 到 IO 线程池。
4. **DDL 协调在 handler 层**：handler 先调 meta store 持久化元数据，再调 data store 创建物理表。
