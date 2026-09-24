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
│  │  Cypher Parser → Binder → PhysicalPlanner (planBound) → Execute │  │
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
| `QueryExecutor` | `query/executor/` | 查询编排（parse → plan → execute），事务管理 |
| `PhysicalPlanner` | `query/physical_plan/physical_planner.hpp` | 逻辑计划 → 物理算子，每种算子有独立 planning 方法 |
| `PhysicalOperator` | `query/physical_plan/physical_operator_base.hpp` | 物理算子抽象基类（火山模型 pull-based 迭代器） |
| 13 种物理算子 | `query/physical_plan/operator/*.hpp` | AllNodeScan/LabelScan/IndexScan/Expand/Filter/Project/Aggregate/Sort/Skip/Limit/Distinct/CreateNode/CreateEdge |
| `LogicalOperator` | `query/logical_plan/logical_operator_fwd.hpp` | variant typedef（前向声明 + unique_ptr 打破循环依赖） |
| 12 种逻辑算子 | `query/logical_plan/operator/*.hpp` | AllNodeScan/LabelScan/Expand/Filter/Project/Aggregate/Sort/Skip/Limit/Distinct/CreateNode/CreateEdge |

关键设计：
- **只依赖 async 接口**：`IAsyncGraphDataStore` + `IAsyncGraphMetaStore`
- **事务通过 async 接口**：`beginTran/commitTran/rollbackTran`
- **协程管道**：Pull-based 火山模型，批量 `AsyncGenerator<DataChunk>`（列存，1024 行/批）

### Server Layer（服务层）

fbthrift RPC 服务 + 交互式 Shell。

| 类 | 文件 | 职责 |
|----|------|------|
| `EuGraphHandler` | `server/` | Thrift handler，DDL 协调，DML 委托 QueryExecutor |
| `EuGraphRpcClient` | `shell/` | Thrift 客户端封装 |
| Shell REPL | `shell/` | 交互式命令行（RPC 连接 server） |

关键设计：
- **EuGraphHandler 依赖 async 接口**：`IAsyncGraphDataStore` + `IAsyncGraphMetaStore`
- **DDL 协调**：handler 先调 `async_meta_.createLabel()` 持久化元数据，再调 `async_data_.createLabel()` 创建物理表

## 线程模型

Thrift IO 池与 Storage IO 池（`IoScheduler` 内部持有）是**两个独立的 `IOThreadPoolExecutor`**，默认规模相同但互不共享。

**Thrift / RPC 模式**：

```
┌──────────────────────────────────────────────────────┐
│  Thrift IO 池 (IOThreadPoolExecutor, "ThriftIO")      │
│  同时作为 handler 执行池（setThreadManagerFromExecutor）│
│  ├─ 接收请求、执行 handler                             │
│  ├─ 解析/计划/算子执行直接在本池线程上跑（不经 Compute） │
│  └─ 每次存储调用 co_await IoScheduler::dispatch 挂起    │
└──────────────────────┬───────────────────────────────┘
                       │ co_viaIfAsync：跨池，不内联
                       ▼
┌──────────────────────────────────────────────────────┐
│  Storage IO 池 (IOThreadPoolExecutor, IoScheduler)    │
│  ├─ WiredTiger 读写操作                               │
│  ├─ Cursor 操作、事务提交/回滚                         │
│  └─ 完成后恢复挂起的协程（执行线程回到 Thrift IO 池）    │
└──────────────────────────────────────────────────────┘
```

该模式下 Compute 池不被使用。

**Bolt 模式**：

```
Bolt EventBase ──scheduleOn(computeExecutor())──▶ Compute 池 (CPUThreadPoolExecutor)
  （socket 网络 IO）                                 │ session 处理 + 解析/计划/算子执行
                                                    │ co_viaIfAsync：跨池，不内联
                                                    ▼
                                              Storage IO 池 (IoScheduler)
                                                WiredTiger 读写
```

Bolt 连接把整个 session 消息处理放到 Compute 池，以便 socket EventBase 腾出来服务其他连接。

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
