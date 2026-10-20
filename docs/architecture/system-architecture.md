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
│  │  WtConnectionStore data_conn    WtConnectionStore meta_conn           │  │
│  │  └─ ({db}/data/)                 └─ ({db}/meta/)                       │  │
│  │  SyncGraphDataStore (bind)       SyncGraphMetaStore (bind)             │  │
│  │  ├─ 数据表: label_fwd_, vprop_   ├─ 元数据表: table:metadata          │  │
│  │  ├─ 边表: etype_, eprop_         └─ KV: M|label:*, M|edge_label:*     │  │
│  │  └─ op(GraphSession&, …)         └─ 管理类 KV 操作                    │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────────┘
```

## 分层设计

### Sync Layer（同步层）

直接封装 WiredTiger API，提供同步阻塞的图操作接口。

| 类 | 文件 | 职责 |
|----|------|------|
| `wt::table_*` | `storage/wt_table_ops.hpp` | WT 表级原语（无状态自由函数：表级 KV、统一 `scan_range`；session 显式传入）；**不创建会话、不持有事务表、不做 DDL 序列** |
| `SchemaDdl` | `storage/schema_ddl.hpp` | 单连接内 DDL 唯一入口（接收 `WtSession&`；内部 schema 锁 + 建/删/ensure；建表失败只回滚本次新建） |
| `SyncGraphDataStore` | `storage/data/` | 图数据 CRUD + 扫描（`op(GraphSession&, …)`）+ DDL（创建/删除物理表） |
| `SyncGraphMetaStore` | `storage/meta/` | 元数据 KV 操作（metadataPut/Get/Scan） |

关键设计：
- **连接所有者唯一**：`GraphInstance` 持有 `WtConnectionStore data_conn / meta_conn`（`{db}/data/`、`{db}/meta/`）；store 只绑定连接
- **事务模型**：`SessionPool` 拥有 `GraphSession`（独立 WT session + 可变事务 + 会话级 cursor 缓存），
  语句通过非拥有 `SessionLease` 借用；未提交释放 ⇒ 自动回滚
- `wt::table_*` 提供无状态表操作/KV；`SchemaDdl` 接收调用方传入的 `WtSession&`，统一 DDL 序列与失败清理

### IO Layer（IO 层 / Async 层）

通过 `IoScheduler` 将同步 WT 调用调度到 IO 线程池，暴露协程接口。

| 类 | 文件 | 职责 |
|----|------|------|
| `IoScheduler` | `storage/io_scheduler.hpp` | IO 线程池封装，dispatch/dispatchVoid |
| `AsyncGraphDataStore` | `storage/data/` | IAsyncGraphDataStore 实现：构造即绑定语句会话的异步 dispatch、DDL、Vertex/Edge 异步操作；`for_session()` 返回轻量值 |
| `AsyncGraphMetaStore` | `storage/meta/` | IAsyncGraphMetaStore 实现：Label/EdgeLabel 管理、ID 分配、GraphSchema |
| `GraphSchema` | `common/schema/graph_schema.hpp` | 内存 schema 对象（label/edge_label 定义、tombstone、ID 计数器） |

关键设计：
- **IoScheduler**：`dispatch()` 将同步调用调度到 IO 线程池，协程挂起直到完成
- **session 归属**：`WT_SESSION` 由**每个 IO 执行上下文独占**（非事务访问用本线程 session，
  热路径无锁）；详见下文「线程模型 → Session / Cursor 的归属与复用」
- **扫描流钉线程**：分多批读取的 `AsyncGenerator` **不在批次之间任选 IO 线程**，而是每批
  `co_viaIfAsync` 回到**创建其 cursor 的那个具体 `EventBase`**（`co_viaIfAsync(池)` 不足以保证）
- **GraphSchema**：元数据服务维护的内存 schema，避免计算层直接访问元数据存储
- 所有 async 方法返回 `folly::coro::Task` 或 `AsyncGenerator`

### Graph Layer（图实例组合层）

图级生命周期与组合根：每个图持有连接、sync/async store、会话池和查询执行器。

| 类 | 文件 | 职责 |
|----|------|------|
| `GraphManager` | `graph/graph_manager.hpp` | 多图生命周期、Catalog、checkpoint 线程、`acquireGraph()` 准入 |
| `GraphInstance` | `graph/graph_manager.hpp` | 单图的连接/store/session_pool/`QueryExecutor` 聚合，按声明逆序安全析构 |
| `GraphLease` | `graph/graph_manager.hpp` | `GraphInstance*` + `GraphUsageGate::Guard`，保证 DROP 等租约归零 |

关键设计：
- **合法向上依赖**：`graph/` 可以同时依赖 `storage/`、`common/` 和 `query/`；`storage/` **不** include `query/` 或 `graph/`
- **executor 归属**：`QueryExecutor` 是图级组合对象；`GraphInstance` 的 `session_pool` 声明在 `executor` 之前，析构时 executor 先于 pool/stores/connections 销毁
- **共享原语下沉**：`GraphUsageGate` 放 `common/concurrency/`（query 的 StreamContext 也持有 Guard）；`GraphSchema` 放 `common/schema/`（storage/meta 的 `AsyncGraphMetaStore` 与 query 共用）

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
- **图查找即准入**：`GraphManager::acquireGraph()` 在同一把 shared_lock 内完成“查表 + usage->tryEnter”，
  返回 `GraphLease`（指针 + guard）；禁止先取裸指针再 `tryEnter`（DROP 竞态 ⇒ UAF）
- **事务通过会话**：`QueryExecutor` 从 `SessionPool` 取非拥有租约，`for_session(GraphSession&)` 绑定本次语句
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
│  ├─ WiredTiger 读写操作 / Cursor 操作                  │
│  ├─ 事务 begin/commit/rollback 由会话持有线程直接执行    │
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

**注意（分批读取的流）**：上图的"Storage IO 池"对**单次**存储调用成立；对**跨多批的扫描流**，
第一批所在的 IO 线程会被记住，后续每批都回到**同一个线程**（而非重新任选）——原因与规则见下节
「Session / Cursor 的归属与并发规则」。

### Session / Cursor 的归属与并发规则

**WT 线程模型（不变）**：`WT_SESSION` / `WT_CURSOR` 不能被并发访问，但可以被不同线程**串行**使用；
WiredTiger 没有 thread-local 状态（hazard pointer 数组在 session 内），因此“必须由创建它的线程使用”
是误解。注意 **cursor 操作即访问其 session**（`cursor.h`：`CUR2S(c)`）——交接 cursor 必须连 session
一起独占。

**所有权与接口的唯一事实来源**：[session-cursor-pool-design.md](../storage/session-cursor-pool-design.md)。
这里只列稳定的并发规则：

* 连接归 `WtConnectionStore`（每图 data/meta 各一）；会话由 `SessionPool` 拥有，语句借出**非拥有**
  `SessionLease`；提交/回滚由语句边界显式完成，未提交释放由池回滚。**无 TLS、无 cursor 池**。
* cursor 由 `CursorProvider` 按**会话**缓存（`CursorHandle`）；扫描用独立 `acquire_scan_cursor`；
  打开失败经 `error()` 显式可见并粘到会话。
* 同一租约同一时刻只属于一个执行流：WT session/cursor 允许**顺序**换线程，**禁止并发**使用。
* 跨多批的扫描流钉在创建其 cursor 的 `EventBase`（每批 `co_viaIfAsync(bound_evb, …)`）；
  **不能**用 `co_viaIfAsync(io_pool)`——线程池 via 不保证回到原线程。
* 收尾顺序：**先销毁算子树/游标，再 commit/rollback**；事务内任何数据 op 失败都被 `StickyError`
  粘住，`commit()` 拒绝并回滚（失败不可被后续成功提交掩盖）。
* DDL 由 `SchemaDdl` 接收显式 `WtSession&` 并在内部 `schemaMutex` 串行化；`WT_SESSION::drop` 要求
  目标表上没有打开的 cursor ⇒ 长流占用时可能 `EBUSY`，DDL 前需 quiesce 或重试。

**并发基线（2026-10-20，complex-9，长连接）**：4 client / compute=4 为 **6.89 ops/s**；
8 client / compute=8 为 **9.87–9.99 ops/s**；8 client / compute=4 饱和于 6.58 ⇒
`compute_threads ≥ 并发客户端数`。详见
[benchmark §9.4](../benchmark/ldbc-snb-sf0.1-comparison.md)。

**未决**：expand 的短命 cursor（每个源点一把）仍有“合并成一把 / 池化”的优化空间；当前结论是
**不做跨调用池化**（WT 自身已有 per-session cursor cache），但“调用粒度极小且高频”的形态需重新实测。
旧的 TLS/per-thread session、全局锁与 POC 数据保留在 git 历史，不再在本文件重复。

### NUMA 亲和（可选，默认关闭）

`EUGRAPH_NUMA_BIND=1` 启用。`src/common/thread/{numa_topology,numa_thread_factory}.hpp`：
按 `/sys/devices/system/node/nodeN/cpulist` 发现拓扑（**不依赖 libnuma**），创建 IO 池线程时
用 `pthread_setaffinity_np` **按节点绑定（不绑核**——节点内多核保留负载均衡与 P99，同时保住
节点内共享 L3）。目标是让"存储读"与"消费它的计算"留在同一节点，避免跨 socket。
**单节点机器上为 no-op**；CPU 池按节点分组与"派发/回算按节点选池"尚待接入。

### 存储层并发验证

| 手段 | 结果 |
|---|---|
| 单元测试 | `tests/common/numa_topology_test.cpp`（ctest，7 组断言：cpulist 解析与节点归属，脱离 WT/硬件） |
| **TSan 闸门** `scripts/check_tsan.sh` | **2026-10-20 复跑（会话池重构后）**：`build/tsan`、8 线程 × 20s 混合查询 ⇒ 未抑制报告 0、存储/session/cursor 相关行 0、服务端存活 ⇒ **PASS**；抑制清单只覆盖 folly/thrift 外部噪声 |
| 结果一致性 | 2026-10-20 复测：原始语料 327,588 点 / 1,477,965 边，LDBC 查询两引擎行数全部一致（见 [benchmark §9.1/§9.3](../benchmark/ldbc-snb-sf0.1-comparison.md)） |
| 性能 | 2026-10-20 复测：complex-9 4/8 client 6.89 / 9.87–9.99 ops/s（旧记录 6.10/8.41，见 benchmark §9.4） |

**环境注意**：本机 `/tmp` 是 7.5G **tmpfs（内存盘）**⇒ 测试数据目录必须放磁盘；直连 WT 的工具前须
**优雅关闭**服务端（强杀后库需 recovery）；TCK 验证必须用 **CI 同款 feature 集**
（`third_party/openCypher/tck/features` + `tests/tck/features-eugraph`）。

**遗留**：多节点机器验证 NUMA 收益；TSan 闸门挂 CI（当前为本地手工跑，2026-10-20 已复跑 PASS）。
其余 2026-09 记录的引擎遗留项（`Memo::copyIn` 深链爆栈、WT checkpoint 崩溃、谓词多属性成本等）
以 [known-defects-todo.md](../query/known-defects-todo.md) 的当前状态为准；未在其中列出的视为历史记录。

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
