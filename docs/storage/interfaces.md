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

**纯读写**接口（设计见
[session-cursor-pool-design.md](session-cursor-pool-design.md) §2）：

```cpp
class ISyncGraphDataStore {
public:
    // DDL — 管理类操作（调用方显式创建 WtSession 并传入；不得是用户事务）
    virtual bool createLabel(WtSession& session, LabelId label_id) = 0;
    virtual bool dropLabel(WtSession& session, LabelId label_id) = 0;
    virtual bool createEdgeLabel(WtSession& session, EdgeLabelId edge_label_id) = 0;
    virtual bool dropEdgeLabel(EdgeLabelId edge_label_id) = 0;

    // 每个数据操作都吃调用方的会话（不建 session、不开/提交事务、不持事务表）
    virtual bool insertVertex(GraphSession& session, VertexId vid,
                              std::span<const std::pair<LabelId, Properties>> label_props) = 0;
    virtual std::optional<Properties> getVertexProperties(GraphSession& session, VertexId vid,
                                                          LabelId label_id) = 0;
    virtual std::optional<PropertyValue> getVertexProperty(GraphSession& session, VertexId vid, LabelId label_id,
                                                           uint16_t prop_id) = 0;
    virtual LabelIdSet getVertexLabels(GraphSession& session, VertexId vid) = 0;
    virtual void scanVerticesByLabel(GraphSession& session, LabelId label_id,
                                     const std::function<bool(VertexId)>& callback) = 0;
    virtual std::unique_ptr<IVertexScanCursor> createVertexScanCursor(GraphSession& session, LabelId label_id) = 0;
    // …其余 Vertex/Edge/Index 操作同形（首个参数都是 GraphSession&）
};
```

实现约定：

- 内部只用 `session.wt_session()` 与 `session.acquire_cursor(table)`；point 读写复用会话级 cursor 缓存，
  扫描用 `acquire_scan_cursor()` 的独立 cursor（同表嵌套扫描/边扫边删是真实路径）。
- cursor 打开失败由 `CursorHandle::valid()/error()` 显式可见；区间迭代统一走 `wt::scan_range()`。
- DDL 统一走 `SchemaDdl(WtSession&, …)`（session 由调用方显式创建；内部 schema 锁；建表失败只回滚本次新建，drop 把 ENOENT 视为成功）。
- 索引扫描走可恢复的 `IIndexScanCursor`（equality/range × 单值/复合）：async 层每批只拉 1024 条，
  内存 O(batch)，不再先物化全集；range 的 `start` 是**排他**下界（与既有语义一致）。
  `lookupVertexByPrimaryKey` 利用“同一 sortable value 下 key 按 entity_id 升序”直接取首条。
- **失败必须粘到会话**：表原语返回 WT 错误码，会话路径一律 `session.fail_from_wt(...)`；即使 op 的
  `bool/optional` 返回值被忽略，`commit()` 也会拒绝提交并回滚（`StickyError` / `TxnErrorClass`）。
  只有 `WT_NOTFOUND`（点查/删除）是正常结果，不粘错误。
- 连接由 `WtConnectionStore` 拥有，store 只 `bind_connection()`；`open()/close()` 不再属于 store。
- 会话只由 `WtConnectionStore::make_session()` 创建；不存在 `open_admin_session()`。
- 建表（`ensure_tables()`）由 `GraphManager` 在绑定连接后调用。

### IAsyncGraphDataStore

文件：`src/storage/data/i_async_graph_data_store.hpp`

```cpp
class IAsyncGraphDataStore {
public:
    // 数据操作签名不变（会话由实现持有，方法上不再出现 txn/session 参数）
    virtual folly::coro::Task<std::optional<Properties>> getVertexProperties(VertexId vid, LabelId label_id) = 0;
    virtual folly::coro::Task<bool> insertVertex(VertexId vid,
                                                 std::span<const std::pair<LabelId, Properties>> label_props) = 0;
    // …DDL / 读写 / 扫描同前
};

class AsyncGraphDataStore : public IAsyncGraphDataStore {
public:
    // 构造即绑定语句会话；每个语句构造一个轻量值（三个引用）
    AsyncGraphDataStore(ISyncGraphDataStore& store, IoScheduler& io, GraphSession& session);
    // 为某条语句构造轻量、绑定该会话的实例（返回值，不堆分配）
    [[nodiscard]] AsyncGraphDataStore for_session(GraphSession& session) const;
};
```

- **没有** `beginTran/commitTran/rollbackTran/setTransaction`：事务归 `GraphSession`，
  由 `QueryExecutor`/协议层通过会话租约驱动。
- 所有 op 在 `io_.dispatch([this]{ return store_.op(session_, …); })` 中执行。
- 每图保留一个共享实例（绑定数据会话）**只用于 `for_session()`**；DDL 由 store 在方法内部
  创建局部 `WtSession`，`SchemaDdl` 内部锁保证同连接 DDL 串行化 ✓。

## 元数据存储接口

### ISyncGraphMetaStore

文件：`src/storage/meta/i_sync_graph_meta_store.hpp`

连接由 `WtConnectionStore` 拥有（`{db}/meta/`）✓；元数据是**管理类**访问
（schema 变更 / 启动扫描 / ID 高水位持久化），每次操作在 store 内创建局部 `WtSession` ✓。

```cpp
class ISyncGraphMetaStore {
public:
    virtual bool metadataPut(std::string_view key, std::string_view value) = 0;
    virtual std::optional<std::string> metadataGet(std::string_view key) = 0;
    virtual bool metadataDel(std::string_view key) = 0;
    virtual void metadataScan(std::string_view prefix,
                              const std::function<bool(std::string_view, std::string_view)>& callback) = 0;

    // DDL（仅元数据；物理表由 data store 负责）
    virtual bool createLabel(LabelId label_id) = 0;
    virtual bool dropLabel(LabelId label_id) = 0;
    virtual bool createEdgeLabel(EdgeLabelId edge_label_id) = 0;
    virtual bool addEdgeLabelProperties(EdgeLabelId edge_label_id, const std::vector<std::string>& prop_names) = 0;
    virtual bool dropEdgeLabel(EdgeLabelId edge_label_id) = 0;
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
                                                           const std::vector<PropertyDef>& properties = {}) = 0;
    virtual folly::coro::Task<bool> addEdgeLabelProperties(const std::string& name,
                                                           const std::vector<std::string>& prop_names) = 0;
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

1. **连接所有者唯一**：`GraphInstance` 持有 `WtConnectionStore data_conn / meta_conn`（`{db}/data/`、`{db}/meta/`），store 只绑定连接；`session_pool` 声明在连接之后 ⇒ 会话先于连接销毁。
2. **compute 层零 sync 依赖**：`QueryExecutor` 和 `PhysicalOperator` 只依赖 `IAsyncGraphDataStore` 和 `IAsyncGraphMetaStore`。
3. **事务通过会话**：`QueryExecutor` 从 `SessionPool` 取非拥有租约，`begin_read/begin_write` 后把 `GraphSession&` 交给 `for_session()`；提交/回滚由语句边界显式调用，未提交释放由池自动回滚。
4. **DDL 协调**：显式 DDL（CREATE LABEL/EDGE LABEL）由 handler 层协调（先调 `async_meta_.createLabel()` 持久化元数据，再调 `async_data_.createLabel()` 创建物理表）。运行时隐式 DDL（CREATE 语句中引用不存在的边类型或属性）由物理算子 `CreateEdgeLabelPhysicalOp` / `AlterEdgeLabelPhysicalOp` 在执行阶段处理。
5. **ISyncGraphMetaStore DDL 方法为 no-op**：元数据只存在自己的 `table:metadata` 中；物理表的创建由 data store 负责。
6. **批量操作使用独立会话+事务**：`GraphService` 的 `batchInsertVertices`/`batchInsertEdges` 各取一个租约、一个写事务，读预检与写同事务，不参与外层 Cypher 事务。
7. **存储层不参与取消**：取消是语句级（compute 层）的概念，归属 `compute::QueryContext`（每语句一份）。早期版本把它作为 `IAsyncGraphDataStore` 的可写字段——而该类型的实例（`QueryExecutor::async_data_`）被一个图里所有查询共享，一旦有人对共享实例 setter，`for_session()` 的拷贝就会让之后每个查询"一出生就是已取消"。现在存储接口上没有令牌、没有 setter，扫描算子通过 `PhysicalOperator::cancellable(gen)` 在消费上游 chunk 时检查（代价是最多多做一个 batch）。详见 [execution-model.md](../query/engine/execution-model.md)。
8. **扫描走游标，不预先物化**：`ISyncGraphDataStore` 上的扫描有回调式（`scanVerticesByLabel(session, label, cb)` / `scanEdges(...)`）与游标式（`createVertexScanCursor` / `createEdgeScanCursor` / `createAllVertexScanCursor`）两套；异步包装（`IAsyncGraphDataStore`）用游标实现分批流式输出，容量为 1024/批，**不把整个匹配集收进 `std::vector` 再吐**。`createAllVertexScanCursor` 服务"不限标签"的全图扫描：它读顶点存在表（vertex-existence key），因此每个顶点恰好出现一次、天然按 vid 升序；按标签扫描则读该标签的前向表（label-forward key），同样是 vid 升序且内部唯一。这两个"升序 + 内部唯一"的性质是上层 `AllNodeScanPhysicalOp` 能用 k 路归并在流式前提下完成去重的前提。早期 `scanAllVertices()` 曾把全部 vid 物化后再分批，首行延迟等于整图扫完、RSS 随图规模线性增长。
