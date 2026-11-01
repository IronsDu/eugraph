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

所有物理算子实现 `executeChunk() -> folly::coro::AsyncGenerator<DataChunk>`（唯一的执行接口，
纯虚）：

- **消费者拉取**：父算子调用 `co_await child_gen.next()` 从子算子拉取数据
- **批量传输**：`DataChunk::DEFAULT_CAPACITY = 1024`，算子累积到 1024 行后 `co_yield`
- **叶子算子**（Scan）：从 `IAsyncGraphDataStore` 的 `AsyncGenerator<vector<VertexId>>` 拉取批次
- **一元算子**（Filter/Project/Expand/Limit/Skip/Distinct）：拉取子算子输出，逐批转换，攒批 yield
- **阻断算子**（Sort/Aggregate）：先全量消费子算子输出，物化到内存，处理后再分批 yield

---

## 三、IO/Compute 分离

### 线程池（当前实现）

Server 进程内有 4 个可配置线程池（另有 WiredTiger 自身的 eviction 线程，由 `--wt-evict-threads-max` 控制）：

| 线程池 | 类型 | 规模参数 | 职责 |
|--------|------|---------|------|
| Compute | `CPUThreadPoolExecutor` | `--compute-threads` | **进程共享**（`GraphManager` 持有）：Bolt session 消息处理；Thrift 查询的解析/绑定/计划/算子执行/批次生成 |
| Thrift IO / handler | `IOThreadPoolExecutor`（`ThriftIO`） | `--thrift-io-threads` | Thrift 网络 IO；经 `setThreadManagerFromExecutor` 同时作为 handler 与流循环执行池 |
| Storage IO | `IOThreadPoolExecutor`（`IoScheduler` 内部） | `--storage-io-threads` | 所有 WiredTiger 调用（scan、get、insert、commit） |
| Bolt EventBase | `BoltServer` | `--bolt-io-threads` | Bolt 连接的网络 EventBase |

Compute 池由 `GraphManager::init` 创建（`NamedThreadFactory("eugraph-compute")`），经
`QueryExecutor::Config::compute_pool` 注入每个图实例，因此 G 个图共享 1 个池，而不是
`G × --compute-threads` 个线程。

### IoScheduler：真正跨线程（2026-10-31 修复）

`IoScheduler::dispatch/dispatchVoid` 用 `co_withExecutor(io_ka_, child)`，**不是**
`co_viaIfAsync(io_ka_, child)`：

- `co_viaIfAsync(executor, Task)` 只给子 Task 的 promise 绑定 executor，随后返回普通的
  `Task::Awaiter`；该 Awaiter 仍在调用方线程内联 resume。修复前实测
  `work_tid == caller_tid`，`cross_thread=0` —— Storage IO 池线程甚至没有被创建。
- `co_withExecutor` 返回 `TaskWithExecutor`，其 Awaiter 通过 `executor->add()` 把子协程
  投递到 Storage IO 池。子协程完成后，`Task::await_transform` 里的
  `co_viaIfAsync(parentExecutor, ...)` 把调用方协程恢复到**调用方自己的 executor**
  （Bolt = Compute 池；Thrift = handler/网络 IO 池；`blockingWait` = 其 driver）。

```
调用方 executor                         Storage IO executor
    │  co_await io_.dispatch(...)           │
    │  (协程挂起)                            │
    │                                        ├─ WT 操作执行
    │◄── continuation 恢复到调用方 executor ──┤
```

契约：`dispatch` 必须从**有 executor 的协程**调用（生产入口全部满足；测试里的
`blockingWait` 会把 Task 绑到其内部 driver）。恢复的是同一 executor/pool，不保证同一
OS 线程；需要线程本地状态的地方不能假设“原线程”。

单次真实跨线程往返约 **10 µs**（`storage_op_bench` 的 `DispatchBench`；修复前 inline
约 0.47 µs）。因此这次修复是**语义正确性**修复，不等于性能收益：它把 IO 从计算线程
移走，但也给每次存储调用增加一次跨线程成本。高频小调用必须靠批量化/减少 dispatch
次数摊薄，见 [storage-operation-costs.md](../benchmark/storage-operation-costs.md)。

### 各模式的线程归属

**Thrift / RPC 模式**：

1. **Thrift IO 池只做网络 IO + 流循环 + 序列化**。handler 本体不再在 IO 线程上执行：
   `EuGraphHandler` 覆盖了全部 `async_tm_*` 方法，把一个 Task 调度到进程共享 Compute
   池执行，并在**连接自己的 EventBase 线程**上调用 `HandlerCallback::complete()`。
2. 为什么必须显式回 EB：fbthrift 5.05.19 的
   `RocketServerConnection::WriteBatcher` 对“非连接 EventBase 线程产生的请求-响应回包”
   要等 EventBase 下一次唤醒才 flush；空闲连接的下一次唤醒是 5 s task-expire timer ⇒
   每个请求-响应 RPC 停顿 **5–7 s**。这不是“handler ThreadManager 指向 Compute”才有的：
   只要 handler 协程在等待存储/计算后恢复到 **池里的另一个 worker**，回包就会落进
   `IOWorkerContext::ReplyQueue` 并触发同样的停顿。b1/b4/b-io/tm-simple1 均实测复现，
   4 个网络 IO 线程也不能解决；只测流式查询会得到假阴性（payload 持续唤醒 EB）。
3. `co_executeCypherImpl`（参数解析、`executeCypher` 的 parse/bind/plan/`prepareStream`）
   在 Compute 池执行；`StreamContext::gen` 经 `service::hopGeneratorTo(...)` 包装：每个
   batch 的 `inner.next()` 用 `co_viaIfAsync(compute, ...)` 绑定到 Compute 池。generator
   的第一段同步代码仍内联在流循环线程（folly `NextAwaitable` 不调度），但它的第一个
   `co_await`（扫描的第一次存储 dispatch）之后就在 Compute 池恢复，后续 batch 也从
   Compute 开始；wrapper 由 fbthrift 流循环在 IO 线程消费，Thrift 序列化也在 IO。
   没有改用“每个 next() 包一个 Task + co_withExecutor”：那能保证第一段也在 Compute，
   但会把协程帧从流上摘下来，fbthrift 取消/销毁流时可能触到已释放状态；`viaIfAsync`
   保持结构化，且是探针验证过的形态。
4. 存储调用经 `IoScheduler` 到 Storage IO 池，完成后回到 Compute 池（算子的 executor）。
   **例外**：rollback（析构路径不能 await，不 fsync）、cursor close、图生命周期
   （create/drop/open/checkpoint）仍按会话/图所有权在语句当前线程同步执行。事务
   begin/commit 已改为经 `IoScheduler` 到 Storage IO 池（`AsyncGraphDataStore::
   beginSession/commitSession`）。stream 的第一个 batch 在 async_tm 路径由 Compute
   预取；后续 batch 从 yield 恢复的第一段同步代码仍可能落在流循环线程，详见
   [thrift-thread-model.md](../../service/thrift-thread-model.md) §3.4。
5. `async_tm_*` 覆盖会**绕过 fbthrift service interceptors**（服务端未安装任何
   interceptor；如将来引入鉴权/审计 interceptor，需要改为在 helper 内显式调用）。
6. 实测：`rpc_integration_tests` 在 4 个网络 IO worker 下 28/28 通过、全套 8.6 s
   （修复前同配置约 141 s，逐 RPC 5 s 级停顿）；3000 行 `UNWIND` 流式查询约 15 ms。
   TCK 3959 scenarios / 3907 passed / 0 failed，769.2 s（旧基线约 1470–1500 s）。
   线程归因：handler/stream 日志在 `ThriftIO*`，算子执行在 `eugraph-compute`，存储
   在 `eugraph-io*`。

**Bolt 模式**：`BoltConnection::dispatchMessage` 通过 `scheduleOn(computeExecutor())`
把整个 session 消息处理放到 Compute 池，以便 socket EventBase 腾出来服务其他连接
（`src/service/bolt/bolt_server.cpp`）。存储调用再从 Compute 池跳到 Storage IO 池并
回到 Compute 池。

> 统计一条语句的 IO 成本：设置 `EUGRAPH_IO_TRACE=1`，语句结束时日志会输出
> `[io-trace] io_dispatches=<N> wt_search=<N> wt_search_near=<N> by_call=<top5> query=<text>`。
> 修复后 `io_dispatches` 是**真实跨线程 dispatch 次数**（`cross_thread` 全部命中），
> 可按约 10 µs/次做上界估算；WT 精确 seek/前缀 seek 约 0.4–0.6 µs，因此
> **dispatch 次数比 seek 次数更值得优化**。单次成本与归因方法见
> [storage-operation-costs.md](../benchmark/storage-operation-costs.md)。

> 算子级耗时：设置 `EUGRAPH_OP_TRACE=1`，语句结束输出每个物理算子的
> `[op-trace] <indent>op=<name> rows=<N> chunks=<N> self_cpu_ms=<x> incl_cpu_ms=<y>
> self_wall_ms=<x> incl_wall_ms=<y>`。`incl_*` 含子树，`self_* = incl_* - 直接子算子 incl_*`；
> CPU 时间来自 `CLOCK_THREAD_CPUTIME_ID`，wall 时间在负载高时包含调度等待，应优先看 CPU。
> 相关子计划（如 PC）的记录按算子实例累计；空结果算子也会出现（rows=0）。
> 该开关默认关闭，关闭时 `executeChunkTraced()` 直接返回原 generator，不额外包协程。
>
> 归因语义（2026-10-29 修正）：记录在算子**第一次被 resume** 时按当时的
> `tls_current_op_trace` 挂到父记录；generator 在 `co_yield` 前把 TLS 还给调用方，
> resume 后再装回自己，因此“先 pull 左孩子、再 pull 右孩子”的 join 不会把右孩子
> 记到左孩子名下（此前 c5 的 `LeftJoin self_cpu` 因此虚高 166 ms）。
> `CLOCK_THREAD_CPUTIME_ID` 是每线程时钟：跨线程 resume 的区间不计 CPU（否则差值为负
> 会让 uint64 计数下溢成 1.8e13 ms），所以 `incl_cpu` 可能小于同一算子的 `incl_wall`。
> 完整的环境变量、字段表、归因/日志量限制见
> [metrics-and-tracing.md](../debugging/metrics-and-tracing.md)。

### 表达式求值不做 IO（2026-10-31 修复）

拓扑阶段（Scan/Expand/VarLenExpand/ShortestPath）产出的 `VertexValue` 可能只有 id，
属性需要从存储补齐；`startNode/endNode` 还会在求值过程中**新造**一个裸顶点。旧实现
在 `eval_property.cpp` 里用 `folly::coro::blockingWait` 现场加载：

- 计算线程被阻塞等 IO；
- 修复 `IoScheduler` 后，`blockingWait` 的 continuation 会落在 Storage IO 线程上，
  与“计算线程不碰 IO”的目标直接冲突；
- 动态标签/`__anon__` 的定义查询同样会现场读 meta。

现在改为**算子边界的两阶段**：

1. `ExpressionEvaluator::prefetch(exprs, chunk)`（协程）在 `evaluate` 之前运行：
   - 扫描 chunk 的所有列（含 list/map/path 内部）收集裸顶点 id，一次
     `batchGetVertexData` 批量加载；
   - 从数据中收集 label / edge-label id，缺失定义才异步查 meta；
   - 对含 `startNode/endNode` 的表达式做 dry pass：先求值一次并记录“求值过程中新造的
     裸顶点 id”，批量加载后再让真正的求值命中缓存（最多 3 轮）。
2. `evaluate`/`evaluatePredicate` 保持同步，但只读缓存，**不含任何 IO**。缓存未命中时
   不再回退到 IO：计数 `ExpressionEvaluator::missingPrefetchCount()` 并返回 NULL；
   测试/TCK 要求该计数为 0。

覆盖点：`Filter/Project/Aggregate/Sort/Unwind/Foreach/Set/CreateNode/CreateEdge/Delete/Merge`
在每批求值前都调用 `prefetch`；`PathElementPropertyRead` 仍走自己的批量加载路径。

### 已知问题与待办

1. **~10 µs/次 dispatch**：修复后存储调用是真实跨线程。c5/c9/c14 这类批量后的查询
   可接受（数百次 dispatch），但逐行 expand/写入等高频小调用需要继续做批量化，否则
   跨线程成本会主导。
2. **fbthrift WriteBatcher 停顿**：不要在不修 fbthrift 的前提下把 handler ThreadManager
   移到非网络 IO executor；任何线程模型改动的验收必须包含**非流式请求-响应 RPC**
   延迟（只测流式查询会假阴性）。
3. **表达式求值缓存**：`prefetch` 缓存按 evaluator 生命周期累加；写查询如果后续读到
   同一顶点的更新数据，需要确认该 operator 不会复用旧缓存（当前写 operator 的读取都
   发生在写入前，测试/TCK 覆盖）。

## 四、流式执行

### StreamContext

`prepareStream()` 返回 `shared_ptr<StreamContext>`，打包了：

- `phys_op` — 物理算子树（unique_ptr，StreamContext 持有所有权）
- `gen` — 从 `phys_op->execute()` 创建的 AsyncGenerator
- `session` — 本语句的 `SessionLease`（**声明在 phys_op/gen 之前 ⇒ 最后销毁**）
- `store` — 共享的异步门面引用（只用于 DDL；数据路径用 `query_store`）
- `query_store` — 本查询 `for_session(*session)` 出来的会话绑定 facade，物理算子实际使用的就是它；它同时承载本查询的取消标志
- `graph_guard` — 图使用租约，保证流消费期间图不被 DROP
- `query_context` — 每语句一份的 `QueryContext`（取消令牌等），算子通过 `shared_ptr` 共享；由 `setQueryContext()` 在规划完成后一次性挂到算子树根上
- `label_defs` / `edge_label_defs` — 被物理算子通过裸指针引用，StreamContext 持有所有权

### Handler 流式生成

`co_executeCypher` 中将 `StreamContext::gen` 包装为 Thrift 流：

```
AsyncGenerator<DataChunk>
  → makeStreamGenerator (valueToThrift 逐行转换)
    → co_yield ResultRowBatch
      → ServerStream<ResultRowBatch>
        → Rocket 传输
          → ClientBufferedStream
```

客户端（Shell RPC 模式）通过 `subscribeInline` 逐批消费。

### 事务生命周期

- 流正常结束：销毁算子树（关游标）后 `co_await StreamContext::commitAsync()`（经
  Storage IO 提交）；提交失败必须报错
- 客户端断连：销毁算子树后 `StreamContext::rollback()`，租约归还池
- 执行期错误：generator 提前结束，回滚
- 查询被取消：按“未正常完成”处理，必须回滚而不是提交（见下）
- 显式事务（Bolt BEGIN…COMMIT）：语句通过 `SessionRef` 加入调用方已开启的会话，语句不 commit；
  由 COMMIT/ROLLBACK 边界负责

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
| Expand | 嵌套循环：对每行输入扫描邻居边，产出 VertexRef / EdgeKey / VertexRef（拓扑） | scanEdges（caller-owned vector，4096/批） |
| VarLenExpand | DFS + 显式栈 + 边唯一性回溯，产出 VertexRef（dst），PathValue（path），List\<EdgeValue\>（edge list） | scanEdges × hop_depth |
| ShortestPath | 独立算子：shortestPath 用双向 BFS（frontier 较小的一侧优先），allShortestPaths 用前向 BFS 枚举等长路径；产出 PathValue | scanEdges（双向 BFS 每层只扫 frontier） |
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

4. **IoScheduler 必须用 `co_withExecutor`，不能用 `co_viaIfAsync`**：对 `Task` 参数，后者只绑定 executor 而内联 resume（修复前 `cross_thread=0`，存储调用实际在计算线程执行）。前者才真正投递到 Storage IO 池；完成后调用方协程由 `Task::await_transform` 恢复到**自己的 executor**（pool 级，不保证同一 OS 线程）。新增/修改 dispatch 路径时用 `tests/test_io_scheduler.cpp` 守住该契约。

5. **事务不再有句柄**：`GraphSession` 由池拥有、租约非拥有；会话归还后不得再使用其 `wt_session()`/cursor。

6. **CREATE INDEX 同步回填**：当前阻塞用户请求直到回填完成，大表会很慢。

7. **先销毁算子树，再结束事务**（顺序红线）：`endStream()` / 析构 `StreamContext` 之前**不要** commit/rollback。顶层 generator 返回空并不代表所有游标都已关闭——`LIMIT` 等提前返回的算子会把子生成器留在 `co_yield` 上，活游标跟着那些协程帧一直活到算子树被销毁。先结束事务会释放 WT session 与游标，随后析构 `WtCursor` 时 `cursor_->close()` 打在已释放内存上（实测 SIGSEGV：`~WtCursor` ← `~VertexScanCursorImpl`）。正确顺序：**销毁流（关游标）→ commit/rollback**。Bolt 侧见 [neo4j-bolt-protocol.md](../../service/neo4j-bolt-protocol.md) 第 10 节。

9. **Thrift 回包必须在连接 EventBase 线程产生**：fbthrift 5.05.19 的
   `RocketServerConnection::WriteBatcher` 对“非连接 EventBase 线程产生的请求-响应回包”
   要等 EB 下次唤醒才 flush（空闲连接 5 s task-expire）⇒ 5–7 s 停顿。因此
   `EuGraphHandler` 覆盖全部 `async_tm_*`：handler 任务跑在 Compute，完成时用
   `runOnComputeAndComplete()` 把 `HandlerCallback::complete()` 投回连接 EB。
   **不要**把 handler ThreadManager 指向 Compute 池（那只是把停顿源换个位置）。
   验收必须包含 4 个网络 IO worker 下的非流式 RPC 延迟。回归测试：
   `tests/test_rpc_integration.cpp` 的 `RequestResponseRpcHasNoMultiSecondStall` /
   `StreamingQueryCompletesWithoutStall`，以及
   `tests/test_query_stream_hop.cpp`（算子批次在 Compute、consumer 在 IO）。

10. **表达式求值不得做 IO**：`ExpressionEvaluator::evaluate/evaluatePredicate` 只读
    `prefetch()` 填好的缓存；`prefetch()` 是算子边界唯一的加载点（含 startNode/endNode
    的 dry pass）。`missingPrefetchCount()` 非 0 表示有 operator 漏调 prefetch，会得到
    NULL 而不是回退到阻塞 IO。

11. **放弃流就必须回滚**：会话租约随 `StreamContext` 返回池，`GraphSession::close()` 会兜底回滚未提交事务；但"丢弃流"的路径仍必须显式销毁算子树后调用 `StreamContext::rollback()`（Bolt 用 `BoltSession::abandonStream()`，Thrift/RPC 用 `StreamAbandonRollback`），否则游标可能晚于会话释放。池耗尽时 `prepareStream()` 直接报错，绝不降级到无事务执行。
