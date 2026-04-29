# 系统架构

> [当前实现] 参见 [README.md](../README.md) 返回文档导航

## 系统架构图

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           EuGraph Process                                    │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │                       fbthrift Server (IO 线程)                        │  │
│  │  EuGraphHandler                                                       │  │
│  │  ├─ DDL: createLabel / createEdgeLabel / listLabels / listEdgeLabels  │  │
│  │  └─ DML: executeCypher → QueryExecutor                                │  │
│  └───────────────────────────────┬───────────────────────────────────────┘  │
│                                  │                                          │
│  ┌───────────────────────────────┼───────────────────────────────────────┐  │
│  │                    Compute Layer (计算线程池)                           │  │
│  │                   (CPUThreadPoolExecutor)                              │  │
│  │                                                                       │  │
│  │  Cypher Parser → LogicalPlanBuilder → PhysicalPlanner (含 IndexScan 优化) → Execute │  │
│  │                                                                       │  │
│  │  依赖: IAsyncGraphDataStore, IAsyncGraphMetaStore                     │  │
│  │  不依赖任何 sync 接口                                                  │  │
│  └───────────────────────────────┬───────────────────────────────────────┘  │
│                                  │ co_await (协程挂起)                       │
│                                  ▼                                          │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │                    IO Layer (IO 线程池 / IoScheduler)                   │  │
│  │                    (IOThreadPoolExecutor)                              │  │
│  │                                                                       │  │
│  │  AsyncGraphDataStore    AsyncGraphMetaStore                           │  │
│  │  ├─ 事务: begin/commit/rollback     ├─ Label CRUD                     │  │
│  │  ├─ DDL: createLabel/EdgeLabel      ├─ EdgeLabel CRUD                 │  │
│  │  ├─ Vertex/Edge CRUD                ├─ ID 分配 (nextVertexId/EdgeId)   │  │
│  │  └─ Scan (批量 AsyncGenerator)      └─ GraphSchema (内存)              │  │
│  └───────────────────────────────┬───────────────────────────────────────┘  │
│                                  │ dispatch 到 IO 线程执行                   │
│                                  ▼                                          │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │                       Sync Layer (WiredTiger)                          │  │
│  │                                                                       │  │
│  │  SyncGraphDataStore              SyncGraphMetaStore                    │  │
│  │  ├─ 独立 WT 连接 ({db}/data/)    ├─ 独立 WT 连接 ({db}/meta/)          │  │
│  │  ├─ 数据表: label_fwd_, vprop_   ├─ 元数据表: table:metadata          │  │
│  │  ├─ 边表: etype_, eprop_         └─ KV: M|label:*, M|edge_label:*     │  │
│  │  └─ 继承 WtStoreBase                                                  │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────────┘
```

## 分层设计

### Sync Layer（同步层）

直接封装 WiredTiger API，提供同步阻塞的图操作接口。

| 类 | 文件 | 职责 |
|----|------|------|
| `WtStoreBase` | `storage/wt_store_base.hpp` | 共享 WT 基类（连接管理、session/cursor 缓存、KV 操作） |
| `SyncGraphDataStore` | `storage/data/` | 图数据 CRUD、事务、DDL（创建物理表）、扫描 |
| `SyncGraphMetaStore` | `storage/meta/` | 元数据 KV 操作（metadataPut/Get/Scan） |

关键设计：
- **独立 WT 连接**：meta store 在 `{db}/meta/`，data store 在 `{db}/data/`
- **事务模型**：每个事务持有独立的 WT session 和 cursor 缓存
- `WtStoreBase` 提供连接管理、表操作、KV 读写等共享基础设施

### IO Layer（IO 层 / Async 层）

通过 `IoScheduler` 将同步 WT 调用调度到 IO 线程池，暴露协程接口。

| 类 | 文件 | 职责 |
|----|------|------|
| `IoScheduler` | `storage/io_scheduler.hpp` | IO 线程池封装，dispatch/dispatchVoid |
| `AsyncGraphDataStore` | `storage/data/` | IAsyncGraphDataStore 实现：事务、DDL、Vertex/Edge 异步操作 |
| `AsyncGraphMetaStore` | `storage/meta/` | IAsyncGraphMetaStore 实现：Label/EdgeLabel 管理、ID 分配、GraphSchema |
| `GraphSchema` | `storage/graph_schema.hpp` | 内存 schema 对象（label/edge_label 定义、tombstone、ID 计数器） |

关键设计：
- **IoScheduler**：`dispatch()` 将同步调用调度到 IO 线程池，协程挂起直到完成
- **GraphSchema**：元数据服务维护的内存 schema，避免计算层直接访问元数据存储
- 所有 async 方法返回 `folly::coro::Task` 或 `AsyncGenerator`

### Compute Layer（计算层）

查询引擎，只依赖 async 接口，不直接访问同步层。

| 类 | 文件 | 职责 |
|----|------|------|
| `QueryExecutor` | `compute_service/executor/` | 查询编排（parse → plan → execute），事务管理 |
| `PhysicalPlanner` | `compute_service/physical_plan/` | 逻辑计划 → 物理算子 |
| `PhysicalOperator` | `compute_service/physical_plan/` | 13 种物理算子（Scan/Expand/IndexScan/Filter/Project/Aggregate/Sort/Skip/Limit/Distinct/CreateNode/CreateEdge） |

关键设计：
- **只依赖 async 接口**：`IAsyncGraphDataStore` + `IAsyncGraphMetaStore`
- **事务通过 async 接口**：`beginTran/commitTran/rollbackTran`
- **协程管道**：Pull-based 火山模型，批量 `AsyncGenerator<RowBatch>`

### Server Layer（服务层）

fbthrift RPC 服务 + 交互式 Shell。

| 类 | 文件 | 职责 |
|----|------|------|
| `EuGraphHandler` | `server/` | Thrift handler，DDL 协调，DML 委托 QueryExecutor |
| `EuGraphRpcClient` | `shell/` | Thrift 客户端封装 |
| Shell REPL | `shell/` | 交互式命令行（embedded / RPC 双模式） |

关键设计：
- **EuGraphHandler 依赖 async 接口**：`IAsyncGraphDataStore` + `IAsyncGraphMetaStore`
- **DDL 协调**：handler 先调 `async_meta_.createLabel()` 持久化元数据，再调 `async_data_.createLabel()` 创建物理表

## 线程模型

```
┌──────────────────────────────────────────────────────┐
│  Thrift IO 线程 (fbthrift IOThreadPoolExecutor)       │
│  ├─ 接收请求                                          │
│  ├─ DDL/DML 通过 IoScheduler::dispatch （server 模式   │
│  │   因 co_viaIfAsync 内联，实际在 IO 线程上执行）     │
│  └─ 嵌入式模式下 DML 通过 co_withExecutor 委托给       │
│       compute 线程池                                  │
└──────────────────────┬───────────────────────────────┘
                       │ co_withExecutor (仅嵌入式模式)
                       ▼
┌──────────────────────────────────────────────────────┐
│  Compute 线程池 (CPUThreadPoolExecutor)               │
│  ├─ 嵌入式模式：解析、逻辑/物理计划、表达式求值        │
│  ├─ Server 模式：co_viaIfAsync 内联，实际在 IO 线程    │
│  └─ 需要 IO 时 co_await 挂起，让出 CPU                │
└──────────────────────┬───────────────────────────────┘
                       │ co_viaIfAsync (IoScheduler)
                       ▼
┌──────────────────────────────────────────────────────┐
│  IO 线程池 (IOThreadPoolExecutor, IoScheduler)        │
│  ├─ WiredTiger 读写操作                               │
│  ├─ Cursor 操作、事务提交/回滚                         │
│  └─ 完成后恢复挂起的协程                               │
└──────────────────────────────────────────────────────┘
```

## DDL 协调流程

DDL 操作（如创建标签）由 handler 协调两个 store：

```
Handler::co_createLabel(name, props):
  1. label_id = co_await async_meta_.createLabel(name, props)
     → 持久化 label def 到 meta store (via IoScheduler)
     → 更新内存 GraphSchema
  2. co_await async_data_.createLabel(label_id)
     → 创建物理数据表 (label_fwd_{id}, vprop_{id})
```

## 服务组合模式

| 模式     | 开启的组件                                | 说明                 |
| ------ | ----------------------------------- | ------------------ |
| 存算一体   | SyncGraphDataStore + SyncGraphMetaStore + IoScheduler + Compute | 单机部署，本地计算，零 RPC 开销 |
| 全功能节点  | 上述 + fbthrift Server + Shell         | 单机全功能 / 分布式协调节点    |
