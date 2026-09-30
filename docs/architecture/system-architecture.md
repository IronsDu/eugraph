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
- **session 归属**：`WT_SESSION` 由**每个 IO 执行上下文独占**（非事务访问用本线程 session，
  热路径无锁）；详见下文「线程模型 → Session / Cursor 的归属与复用」
- **扫描流钉线程**：分多批读取的 `AsyncGenerator` **不在批次之间任选 IO 线程**，而是每批
  `co_viaIfAsync` 回到**创建其 cursor 的那个具体 `EventBase`**（`co_viaIfAsync(池)` 不足以保证）
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

**注意（分批读取的流）**：上图的"Storage IO 池"对**单次**存储调用成立；对**跨多批的扫描流**，
第一批所在的 IO 线程会被记住，后续每批都回到**同一个线程**（而非重新任选）——原因与规则见下节 R3。

### Session / Cursor 的归属与复用（存储层并发正确性的基础）

**背景**：`WT_SESSION` / `WT_CURSOR` 非线程安全。官方文档（`third_party/wiredtiger/src/docs/threads.dox`）
明确两点：**不能被并发访问**，但**可以被不同线程串行使用**；且"**WiredTiger 没有 thread-local 状态**"
（hazard pointer 数组是 `WT_SESSION` 的成员，session/cursor 结构内无线程身份字段）。因此
"必须由创建它的物理线程使用"是**误解**；官方也**从未**要求跨线程前必须 `reset()`。
注意 **cursor 操作即访问其 session**（`cursor.h`：`CUR2S(c)` 从 cursor 取 session）——要交接 cursor
就必须**连 session 一起交接并独占**。

**曾经的缺陷（已修）**：`getSession(INVALID_GRAPH_TXN)` 返回共享 `defaultSession_`，点查路径加锁使用它，
而 7 处批量/扫描路径（`sync_graph_data_store.cpp:433/466/664/1134/1207/1299/1372`）**完全没加锁**
⇒ 同一 session 可被两线程并发使用（表现为 WT 断言 `lock_success == 0` → abort）；同时那把全局锁是
全进程唯一、而每次属性点查都走它，且引擎/服务层从不调用 `beginTransaction()` ⇒ 必然全程争用
（8 线程吞吐 0.65 M/s，**低于**单线程 1.64）。此外 4 条扫描流（`scanVerticesByLabel` /
`scanAllVertices` / `scanEdgesByType` / `scanEdges`）在 `io_.dispatch`（线程 A）里创建 cursor
（绑定 A 的 session），后续每批却由 `dispatchVoid` 分派到**任意线程**使用。

**现行规则**：

| # | 规则 |
|---|---|
| R1 | session **每个 IO 执行上下文独占**、长期持有（数量 = IO 线程数，不随并发增长）；非事务访问一律用本线程 session，热路径**无锁** |
| R2 | cursor **归一条读流**；流内跨批次**不 reset**（保持已定位 ⇒ `next()` 为 O(1)，无需 `search_near` 重定位） |
| R3 | **流钉在创建其 cursor 的具体 `EventBase`**：每批 `co_viaIfAsync(bound_evb, …)`。**不能**用 `co_viaIfAsync(io_pool)`——对线程池 via 不保证回到原线程 |
| R4 | 流结束 / 取消 / 异常：归队 → `reset()` → 关闭（RAII 或 `co_scope_exit` 兜底） |
| R5 | cursor 生命周期 **⊆ 单次调用**（批量路径一次 `open_cursor` 覆盖整批）；**不做跨调用池化**（见下方实测） |
| R6 | 所有权：TLS 只放**非拥有句柄**，session 归 store 拥有并在 `conn_.close()` **之前**统一释放（线程退出可能晚于连接关闭） |
| R7 | 归还语义：不再使用的 cursor **`close()`**（当前做法）；若要缓存复用则必须先 `reset()`（`reset()` 本职是释放 page pin，与线程无关） |

**实测与取舍**：

| 项 | 结果 | 决定 |
|---|---|---|
| 一次 cursor 覆盖整批 | 属性密集查询 **−27%**（cursor 开关原占单次点查 **51.6%**） | ✅ 采纳 |
| executor 独占 session + 去全局锁 | 存储层 8 线程 **0.65 → 4.88 M/s（7.5×）**；引擎 4 线程 QPS 2.51 → 6.44 ops/s；每查询 CPU 1227 → 609 ms；消除上述 7 处竞争 | ✅ 采纳 |
| 4 条扫描流钉 EventBase | 结果与基线一致；**TSan 闸门 PASS** | ✅ 采纳 |
| 线程配比 | 瓶颈在 **compute**：4→8 使 8 线程 QPS 5.51 → 9.36（io 2 vs 8 无差异），天花板 ~9.5 ops/s | ✅ **`compute_threads ≥ 并发客户端数`** |
| 存储内联（去 IO 跳转） | 8 线程 **−4.3%** | ❌ 否决 |
| cursor 池（跨线程共享 / 跨调用复用：4 个变体） | 崩溃 ×2、**−8~15%** ×2 | ❌ 否决 |

**五个负结果的统一根因**：**WT 自身已有 per-session cursor cache**——"同一 session 反复开同一 URI"
本已被 WT 缓存，自建池是在本已便宜的操作上叠加哈希查找与容器操作。
⇒ cursor 的正确形态是"**调用内复用（跨批次保持位置）**"，不是"跨调用池化"。
（适用边界：若将来出现"**调用粒度极小且高频**"的形态，需重新测量——本阶段未覆盖。）

**计算侧不绑定**：每批回到的 compute 线程可能是任意 worker（不涉及 session/cursor，无正确性问题）。
代价是 IO 线程写出的 chunk 可能被另一个核读取（**跨核搬运 dirty line**；同节点 L3 仍命中）。
不消除它的原因：唯一干净做法是"同核生产+消费"= 存储内联，实测 −4.3%；而钉核会恶化 P99。

### 待定议题：**短命 cursor 场景**是否值得引入 cursor 池（需实测，尚未做）

上面"不做跨调用池化"的结论有**明确的适用边界**，不要外推：

| 场景 | cursor 寿命 | 每次 open 的开销是否被摊薄 | 池化价值 |
|---|---|---|---|
| **长时间扫描**（如整表/索引扫描，一把 cursor 拉整段） | 整条流 | **被摊薄**（一次 open vs 毫秒~秒级工作） | ❌ 无价值（POC-10 实测 −7~15%） |
| **短命 cursor**（如 **expand 的邻接扫描：每个源点一把 cursor**） | 只覆盖一个点的少量边 | **不被摊薄** | ⚠️ **可能值得，待测** |

**具体例证**（`src/storage/data/async_graph_data_store.hpp`，`scanEdgesBatch`）：

```cpp
while (offset < vids.size() && batch.size() < BATCH) {
    VertexId vid = vids[offset++];
    auto cursor = store_.createEdgeScanCursor(txn, vid, dir, filter);   // ← 每个源点开一把
    while (cursor->valid() && batch.size() < BATCH) { batch.push_back(...); cursor->next(); }
}                                                                        // ← 用完即关
```

即 **`open_cursor` / `close` 次数 = 源点个数**。当 expand 的邻接点很多时，这类"**大量短命 cursor**"
的每查询开关次数可达数十万次，此时 **POC-1 的量级重新适用**：
**cursor 开关占单次小查询的 51.6%**（0.744 → 0.360 µs）。

**处置顺序（先穷尽"不需要池"的办法，再考虑池）**：

1. **首选：把 N 把短命 cursor 合并成 1 把** —— 即"一次 `open_cursor` + 按源点 `set_key`/`search` 重定位"
   （与 `loadVertexProperties` 路径在 POC-2 中已验证的 `getVertexPropertiesBatchProjected` 同构：
   一把 cursor 覆盖整批）。**不需要池**，直接消除 N−1 次开关；
2. 若某些访问形态无法这样合并（例如每个点的扫描范围不同、需要独立游标状态），
   **再考虑"每执行上下文私有的 cursor 池"**——注意它必须满足既有约束：
   **不跨线程**（R1/R3）、归还前**必 `reset()`**（R7）、**idle 上限 + 关闭**、取消/异常路径必须归还；
   **绝不**回到"跨线程共享池"（POC-7 实测崩溃）；
3. **判据（必须实测）**：选一个 expand 密集的真实查询（高连接度顶点、或 complex-9 的
   `HAS_CREATOR` 展开），做**同二进制、多轮交错取 min** 的 A/B；
   **开关次数下降但吞吐改善 < 5%** ⇒ 不值得引入池；否则先做第 1 步、再评估第 2 步。

**同时要记住的独立问题**：那 9 个**索引扫描**生成器目前是"**一次性把整个匹配集物化**"
（`std::vector<VertexId> all` + 回调跑完再分块 `co_yield`），代价是
**内存 O(匹配集)、无流式性（首批要等全扫完）、无法提前取消、且整段扫描独占一个 IO 线程**。
修法是给同步层加**可恢复的索引游标**（与 `createVertexScanCursor` 同构）；一旦改成跨批持有 cursor，
**它们也必须同时加 R3 的 EventBase 钉扎**——与本节的池化议题是两件独立但会合流的事。

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
| **TSan 闸门** `scripts/check_tsan.sh` | 8 线程 × 15s 混合查询 ⇒ **未抑制报告 0、存储/session/cursor 相关行 0、服务端存活 ⇒ PASS**；抑制清单 `tests/tsan/tsan.supp` 只覆盖 folly/thrift 外部噪声（5 类，均有 addr2line 佐证），**不抑制 eugraph 存储符号** |
| 结果一致性 | `count(Message)=286744`、`count(所有点)=330126`、`count(KNOWS)=14073`、两跳好友=451 |
| 性能 | complex-9 4/8 线程 6.10/8.41 ops/s；全表 3 属性 2.91/4.83 ops/s |

**环境注意**：本机 `/tmp` 是 7.5G **tmpfs（内存盘）**⇒ 测试数据目录必须放磁盘；直连 WT 的工具前须
**优雅关闭**服务端（强杀后库需 recovery）；TCK 验证必须用 **CI 同款 feature 集**
（`third_party/openCypher/tck/features` + `tests/tck/features-eugraph`）。

**遗留**：多节点机器验证 NUMA 收益；把 TSan 闸门挂进 CI；优化器 `Memo::copyIn` 深链爆栈、
WT 检查点 `__wt_ckptlist_saved_free` SEGV、谓词多属性 +235 ms/属性、WT 内部并发成本
（见 `docs/query/known-defects-todo.md` §12/§12.2/§9）。

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
