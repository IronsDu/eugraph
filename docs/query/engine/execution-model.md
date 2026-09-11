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

---

## 四、流式执行

### StreamContext

`prepareStream()` 返回 `shared_ptr<StreamContext>`，打包了：

- `phys_op` — 物理算子树（unique_ptr，StreamContext 持有所有权）
- `gen` — 从 `phys_op->execute()` 创建的 AsyncGenerator
- `txn` — 事务句柄
- `store` — 数据存储引用（用于结束时 commit）
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

---

## 五、各算子执行特征

| 算子 | 特征 | IO 操作 |
|------|------|---------|
| AllNodeScan | 扫描所有标签，去重，产出 VertexRef（拓扑） | scanVerticesByLabel × N |
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

---

## 六、关键不变量与易错点

1. **StreamContext 生命周期必须覆盖整个 generator 消费期**：销毁 StreamContext 会导致物理算子裸指针悬挂。Handler 中通过 lambda 捕获 `shared_ptr<StreamContext>` 保证生命周期。

2. **Sort/Aggregate 是阻断算子**：无界数据集会导致 OOM，当前无溢写磁盘机制。

3. **全量物化**：调用方若 drain 整个 generator 到内存后才 commit，大结果集内存压力大。

4. **`co_viaIfAsync` 跨池不内联**：Thrift IO 池与 Storage IO 池是两个独立 executor，每次存储调用都会跳转到 Storage IO 池。Thrift / RPC 模式下物理算子树在这两个 IO 池之间切换执行，无计算隔离，且 Compute 池不被使用；只有 Bolt 模式会把 session 处理放到 Compute 池。

5. **GraphTxnHandle 是 `void*`**：指向堆上 `TxnState`，commit/rollback 后释放。任何后续访问是 use-after-free。

6. **CREATE INDEX 同步回填**：当前阻塞用户请求直到回填完成，大表会很慢。
