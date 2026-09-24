# 运行时执行模型

> [当前实现] 参见 [README.md](../../README.md) 返回文档导航

---

## 一、整体流水线

```
Cypher 文本 → Parser → AST（含 EXPLAIN） → Binder → BoundLogicalPlan
→ PhysicalPlanner (planBound) → PhysicalOperator 树
→ executeChunk() → AsyncGenerator<DataChunk>
→ (streaming) Thrift ServerStream → Client
```

执行路径：`QueryExecutor::prepareStream()` → `StreamContext` → `makeStreamGenerator` → fbthrift `ServerStream<ResultRowBatch>`。全量物化由调用方自行 drain generator。

---

## 二、Pull-based 火山模型

所有物理算子实现 `execute() -> folly::coro::AsyncGenerator<RowBatch>`：

- **消费者拉取**：父算子调用 `co_await child_gen.next()` 从子算子拉取数据
- **批量传输**：`RowBatch::CAPACITY = 1024`，算子累积到 1024 行后 `co_yield`
- **叶子算子**（Scan）：从 `IAsyncGraphDataStore` 的 `AsyncGenerator<vector<VertexId>>` 拉取批次
- **一元算子**（Filter/Project/Expand/Limit/Skip/Distinct）：拉取子算子输出，逐批转换，攒批 yield
- **阻断算子**（Sort/Aggregate）：先全量消费子算子输出，物化到内存，处理后再分批 yield

---

## 三、IO/Compute 分离

### 线程池

Server 进程内有 4 个可配置线程池（另有 WiredTiger 自身的 eviction 线程，由 `--wt-evict-threads-max` 控制）：

| 线程池 | 类型 | 规模参数 | 职责 |
|--------|------|---------|------|
| Compute | `CPUThreadPoolExecutor` | `--compute-threads` | 仅 Bolt 模式使用：承载整个 Bolt session 消息处理（解析、计划、执行） |
| Thrift IO / handler | `IOThreadPoolExecutor`（`ThriftIO`） | `--thrift-io-threads` | Thrift 网络 IO；同时经 `setThreadManagerFromExecutor` 用作 handler 执行池 |
| Storage IO | `IOThreadPoolExecutor`（`IoScheduler` 内部） | `--storage-io-threads` | 所有 WiredTiger 调用（scan、get、insert、commit） |
| Bolt EventBase | `BoltServer` | `--bolt-io-threads` | Bolt 连接的网络 EventBase |

Thrift IO 池与 Storage IO 池是**两个独立对象**：`GraphManager::init` 内部创建 `IoScheduler`，由后者自持一个 `IOThreadPoolExecutor`（`src/storage/graph_manager.cpp`）；Thrift 的池则在 `src/program/server/eugraph_server_main.cpp` 单独创建。二者默认值相同（均为 4），但互不共享。

### IoScheduler

`IoScheduler::dispatch(func)` 将同步 WT 调用调度到 Storage IO 线程池：

```
调用方线程                        Storage IO 线程
    │                               │
    ├─ co_await io_.dispatch(...) ──┤
    │  (协程挂起)                    │
    │                               ├─ WT 操作执行
    │                               │
    │◄──────── 结果返回 ─────────────┤
    │  (协程恢复)                    │
```

`co_viaIfAsync` 的语义是：**仅当调用方当前就在同一个 executor 上时才内联执行**，否则调度过去。因此跨池调用必然产生一次线程跳转。

### 各模式的线程归属

**Thrift / RPC 模式**：handler 直接运行在 Thrift IO 池上（该池同时是 Thrift 的网络 IO 池），不经过 Compute 池。算子执行中的每次存储调用都经 `IoScheduler::dispatch`，由于当前线程不属于 Storage IO 池，`co_viaIfAsync` 不会内联，而是把调用调度到 Storage IO 池，完成后再恢复原协程。

结果是：物理算子树的执行线程在 Thrift IO 池与 Storage IO 池之间来回切换，没有计算隔离；并且 Thrift 模式下**没有任何代码把查询执行调度到 Compute 池**（`eugraph_handler` 中不存在 executor 调度），因此 `--compute-threads` 在纯 Thrift 模式下不生效。

**Bolt 模式**：`BoltConnection::dispatchMessage` 通过 `scheduleOn(computeExecutor())` 把整个 session 消息处理放到 Compute 池，以便 socket EventBase 腾出来服务其他连接（`src/service/bolt/bolt_server.cpp`）。存储调用再从 Compute 池跳转到 Storage IO 池。因此 Bolt 模式下 `--compute-threads` 才是实际承载查询执行的线程池。

> Bolt 之所以能"整条消息都在 Compute 池上"，是因为它把响应整体物化成 `std::vector<uint8_t>` 后才 `via(socket EventBase)` 写回；Thrift 是流式 RPC，generator 由框架在消费方线程恢复，**不能照搬 Bolt 的做法**（见下方待办 2）。

### 已知问题与待办

> 以下均已实测，结论明确但尚未解决。**动线程归属之前请先读完本节**，否则很容易重复踩坑。

**待办 1：Compute 池目前每图一个，应改为进程一个。**

`GraphManager::openGraphInstance` 按 `compute_threads_` 为每个图实例各建一个 `CPUThreadPoolExecutor`（`src/storage/graph_manager.cpp`），因此 G 个图 = `G × --compute-threads` 个线程，而 Bolt 只用得到当前图那一个。改法是让 `GraphManager` 持有一个共享池（与既有的 `io_scheduler_` 同样做法），经 `QueryExecutor::Config` 注入。参考实现见提交 `f2b3884`（该提交同时含下方待办 2 的失败尝试，回退时被一并撤销）。

**待办 2：把 Thrift 的查询执行移出 IO 线程（暂缓，风险已知）。**

动机：Thrift 模式下 handler 与查询执行都占用 IO 线程。已实测**两次失败尝试**，表现都是秒级停顿，根因相同——**跨线程唤醒 IO worker 的 EventBase 在本版本 fbthrift/folly 下会被延迟数秒**：

| 尝试 | 做法 | 实测结果 |
|------|------|---------|
| 整段 handler 外派 | `setThreadManagerFromExecutor(compute_pool)` | 请求-响应型 RPC（DDL 等）**回包**延迟 6–7 秒：单用例 1.4s → 12.7s，套件 20s → 262s。原因是响应需跨线程写入 `IOWorkerContext::ReplyQueue`。**流式查询走另一条回包路径、完全不受影响，只测查询会得到假阴性。** |
| 仅外派计划阶段 | 在 `co_executeCypher` 内 `co_withExecutor` 搬到 Compute 池，再用 `co_current_executor` + `co_withExecutor` 跳回 IO 线程后返回 | DDL 路径正常（0.5s），但 **TCK 全面退化**：单查询中位耗时 5780ms（425 个样本中 318 个 ≥3s），`tck_tests` 从 1470–1500s 涨到 >1980s 仍未结束。原因是 `StreamContext::gen` 在 Compute 线程上创建、却由 Thrift 在 IO 线程消费，**每批 `gen.next()` 都要跨线程恢复**。 |

结论：**在解决跨线程唤醒延迟之前，不要改 Thrift 的线程归属。** 将来若要推进，需要先弄清该唤醒延迟的机制（`IOWorkerContext::ReplyQueue` 基于 `EventBaseAtomicNotificationQueue::startConsumingInternal`），或改走 Bolt 那种"整条消息物化、只把最终字节写回 EventBase"的模型。

---

## 四、流式执行

### StreamContext

`prepareStream()` 返回 `shared_ptr<StreamContext>`，打包了：

- `phys_op` — 物理算子树（unique_ptr，StreamContext 持有所有权）
- `gen` — 从 `phys_op->execute()` 创建的 AsyncGenerator
- `txn` — 事务句柄
- `store` — 数据存储引用（用于结束时 commit）
- `query_store` — 本查询 `forkTransaction()` 出来的 store 包装器，物理算子实际使用的就是它；它同时承载本查询的取消标志
- `query_context` — 每语句一份的 `QueryContext`（取消令牌等），算子通过 `shared_ptr` 共享；由 `setQueryContext()` 在规划完成后一次性挂到算子树根上
- `label_defs` / `edge_label_defs` — 被物理算子通过裸指针引用，StreamContext 持有所有权

### Handler 流式生成

`co_executeCypher` 中将 `StreamContext::gen` 包装为 Thrift 流：

```
AsyncGenerator<RowBatch>
  → makeStreamGenerator (valueToThrift 逐行转换)
    → co_yield ResultRowBatch
      → ServerStream<ResultRowBatch>
        → Rocket 传输
          → ClientBufferedStream
```

客户端（Shell RPC 模式）通过 `subscribeInline` 逐批消费。

### 事务生命周期

- 流正常结束：generator 耗尽后 `commitTran`
- 客户端断连：generator 销毁，事务隐式回滚（WT session 关闭）
- 执行期错误：generator 提前结束，隐式回滚
- 查询被取消：按“未正常完成”处理，必须回滚而不是提交（见下）

### 查询取消（协作式）

长查询要能被放弃（典型场景：客户端断开；将来也可以是显式的 CANCEL 消息）。实现方式是**显式标志 + 批次边界检查**，不是协程中断：

- `QueryContext`（`src/query/physical_plan/query_context.hpp`）是**每语句一份**的执行上下文，目前持有取消令牌（`QueryCancel = shared_ptr<const atomic<bool>>`，定义也在这里），将来的 deadline / 预算 / 统计也应该加在这里。`prepareStream(query, params, cancel)` 造出它并 `setQueryContext()` 挂到算子树根上，基类递归下发给所有子算子；算子通过 `shared_ptr` 持有，所以拆解顺序不影响正确性。
  - **它刻意不持有 store**：算子自己就有 `store_`；而 `StreamContext` 的 `query_store` 声明在 `phys_op` 之后（先析构），一个被算子保活的 context 若引用它会在极端顺序下悬空。
  - **它的析构函数不做任何事**：结束事务属于显式拆流路径（`BoltSession::abandonStream()` / `StreamAbandonRollback`）。如果让 context 析构顺手回滚，就会在算子析构（关游标）之前释放 WT session，正好踩中下面第 7 条的顺序红线。
  - **存储层完全不知道"取消"这回事**：`IAsyncGraphDataStore` 上既没有令牌也没有 setter（曾经有，见 [interfaces.md](../../storage/interfaces.md) 设计决策 7）。存储实例被一个图里所有查询共享，查询级状态不该落在它身上。
- 算子通过基类提供的 `cancelled()` 检查（无 context 时恒为 false，便于单测直接构造算子），因此**不需要修改任何算子的函数签名或构造函数**。
- 检查点全部在算子侧，只有**一个原语**：`PhysicalOperator::cancellable(gen)`（`physical_operator_base.hpp` 里的模板协程），凡是"按 chunk 消费上游"的地方都包一层，两侧来源各包一次：
  - **存储侧来源**：所有 `store_.scanXxx(...)` 生成器的消费点（9 个算子文件、34 处）；
  - **算子侧来源**：所有 `child_->executeChunk()` / `->execute()` 的获取点（29 个算子文件、38 处）。
  第二条是必需的：**阻断型算子**（`Sort` / `Aggregate`）在"消费完整个子算子输出"期间不会回到上游，此时取消只能靠它自己的检查点；写查询尤其重要——`MATCH (n) CREATE ...` 的根算子不产出任何行，只有算子树内部的检查才能让它停下。
  - **精度**：代价最多是多做一个 batch——飞行中的那个 chunk 会被产出后丢弃，随后算子返回、生成器销毁。唯一会超出"一个 batch"的是**顶点索引扫描**：存储层的 `scanVerticesByIndex*` / `scanVerticesByIndexId*` 仍是单次 dispatch（同步索引 API 没有可续游标），把该索引值的全部匹配收完才开吐；对选择性索引这个量很小，这是"让存储层保持与查询无关"所付的代价。扫描算子本身已不再是例外：`AllNodeScanPhysicalOp` 与 `IndexScanValuesPhysicalOp` 都改为流式（见下节），`scanAllVertices()` 也已换成游标式分批。
  - 粒度到此为止，不打断单次长 IO。行级检查仍保留在 `expand` / `varlen_expand`（单行就能炸开整棵遍历）。
- 被取消的算子直接 `co_return`，生成器提前结束，父算子自然退出，不需要异常传播。
- **收敛即回滚**：生成器“提前结束”和“真的取完”在协议层都表现为 `next()` 取空，因此结束流时必须能区分二者。Bolt 侧用 `streamCancelled()` 显式判定，走与断连相同的分支：回滚事务、结束流、回 `FAILURE`，绝不提交客户端没看全的写事务。

---

## 五、各算子执行特征

| 算子 | 特征 | IO 操作 |
|------|------|---------|
| AllNodeScan | 流式扫描，多标签时按 vid k 路归并去重（输出顺序为 vid 升序），产出 VertexRef（拓扑） | createAllVertexScanCursor（不限标签）/ scanVerticesByLabel × N |
| LabelScan | 按标签 ID 扫描，产出 VertexRef（拓扑） | scanVerticesByLabel |
| IndexScan | 等值或范围扫描，通过 IndexKeyCodec | scanVerticesByIndex / scanVerticesByIndexRange |
| ProjectionExtract | 融合点/边属性抽取 + Project 语义。7 种 ColumnSpec：Passthrough / LoadVertexProp / LoadEdgeProp / LoadVertexLabels / LoadEdgeType / ConstructVertex / ConstructEdge | getVertexLabels / getVertexProperty / getEdgeProperty 等 |
| PathElementPropertyRead | Column Replacement：PathTopology → PathValue（全量加载元素属性和标签，仅用于 PathBuild 路径） | getVertexLabels / getVertexProperties / getEdgeProperties |
| Expand | 嵌套循环：对每行输入扫描邻居边，产出 VertexRef / EdgeKey / VertexRef（拓扑） | scanEdges |
| VarLenExpand | DFS + 显式栈 + 边唯一性回溯，产出 VertexRef（dst），PathValue（path），List\<EdgeValue\>（edge list） | scanEdges × hop_depth |
| Filter | 纯计算，无 IO | 无 |
| Project | 纯计算，无 IO | 无。无 RETURN 时空 ProjectOp 输出 0 列 |
| Sort | **阻断**：全量物化后 `std::sort`。在 Project 之前执行，可引用原始列 | 无 |
| Aggregate | **阻断**：按 group key 哈希聚合 | 无 |
| Distinct | 流式：`unordered_set<Row>` 去重 | 无 |
| Skip | 跳过前 N 行后透传 | 无 |
| Limit | 计数到 limit 后 `co_return` | 无 |
| CreateNode | 逐行创建：child 每行触发一次创建，动态 VID，`__anon__` 轻量属性注册，输出 = child 列 + 新顶点列 | insertVertex + insertIndexEntry + nextVertexId + getOrCreateAnonPropId |
| CreateEdge | 逐行创建：child 每行触发一次创建，动态 EID，src/dst VID 从 DataChunk 解析，输出 = child 列 + 新边列 | insertEdge + nextEdgeId |
| Foreach | 逐行求值列表后**逐元素**跑 body 子计划（相关源注入外层列 + 元素）并排空它（只取副作用）；body 发布过的实体按 id 回灌外层行，输入行原样透传（基数不变）。列表为空/null 时无操作；非列表值按单元素处理 | body 内各写算子的 IO |

---

## 六、关键不变量与易错点

1. **StreamContext 生命周期必须覆盖整个 generator 消费期**：销毁 StreamContext 会导致物理算子裸指针悬挂。Handler 中通过 lambda 捕获 `shared_ptr<StreamContext>` 保证生命周期。

2. **Sort/Aggregate 是阻断算子**：无界数据集会导致 OOM，当前无溢写磁盘机制。

3. **全量物化**：调用方若 drain 整个 generator 到内存后才 commit，大结果集内存压力大。

4. **`co_viaIfAsync` 跨池不内联**：Thrift IO 池与 Storage IO 池是两个独立 executor，每次存储调用都会跳转到 Storage IO 池。Thrift / RPC 模式下物理算子树在这两个 IO 池之间切换执行，无计算隔离，且 Compute 池不被使用；只有 Bolt 模式会把 session 处理放到 Compute 池。

5. **GraphTxnHandle 是 `void*`**：指向堆上 `TxnState`，commit/rollback 后释放。任何后续访问是 use-after-free。

6. **CREATE INDEX 同步回填**：当前阻塞用户请求直到回填完成，大表会很慢。

7. **先销毁算子树，再结束事务**（顺序红线）：`endStream()` / 析构 `StreamContext` 之前**不要** commit/rollback。顶层 generator 返回空并不代表所有游标都已关闭——`LIMIT` 等提前返回的算子会把子生成器留在 `co_yield` 上，活游标跟着那些协程帧一直活到算子树被销毁。先结束事务会释放 WT session 与游标，随后析构 `WtCursor` 时 `cursor_->close()` 打在已释放内存上（实测 SIGSEGV：`~WtCursor` ← `~VertexScanCursorImpl`）。正确顺序：**销毁流（关游标）→ commit/rollback**。Bolt 侧见 [neo4j-bolt-protocol.md](../../service/neo4j-bolt-protocol.md) 第 10 节。

8. **放弃流就必须结束它的事务**：`GraphTxnHandle` 只被 commit/rollback 从 `txns_` 里摘除，句柄一丢，事务的 WT session、快照与未提交修改就活到进程结束。累积约 185 个泄漏的事务后 `open_session()` 失败，`beginTran()` 返回 `INVALID_GRAPH_TXN`，`getSession(INVALID_GRAPH_TXN)` 兜底到共享 `defaultSession_`（autocommit）——**查询静默失去事务语义，写操作逐行提交且无法回滚**，最终 WT 断言 `lock_success == 0` 崩溃。因此"丢弃流"的路径必须调用 `rollbackTranNow()`：Bolt 用 `BoltSession::abandonStream()`，Thrift/RPC 用 `eugraph_handler.cpp` 里的 `StreamAbandonRollback`；`prepareStream()` 也改为在 `beginTran()` 失败时直接报错而不是降级。
