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

| 线程池 | 类型 | 职责 |
|--------|------|------|
| Compute | `CPUThreadPoolExecutor` | 解析、逻辑/物理计划、表达式求值、过滤、排序 |
| IO | `IOThreadPoolExecutor` | 所有 WiredTiger 调用（scan、get、insert、commit） |

### IoScheduler

`IoScheduler::dispatch(func)` 将同步 WT 调用调度到 IO 线程池：

```
Compute 线程                     IO 线程
    │                               │
    ├─ co_await io_.dispatch(...) ──┤
    │  (协程挂起)                    │
    │                               ├─ WT 操作执行
    │                               │
    │◄──────── 结果返回 ─────────────┤
    │  (协程恢复)                    │
```

`co_viaIfAsync` 检测：若调用方已在 IO 线程上，则**内联执行**（无线程跳转）。

### Server 模式的特殊性

Thrift server 的 handler 和 IO 共用同一 `IOThreadPoolExecutor`。因此 RPC 模式下 handler 已在 IO 线程上，`IoScheduler::dispatch` 全部内联——物理算子执行（包括表达式求值）实际上都在 IO 线程上运行，Compute 线程池未被使用。

Server 模式下 handler 已在 IO 线程上，`IoScheduler::dispatch` 全部内联。调用方可自行通过 `co_withExecutor` 调度到 Compute 线程池。

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

4. **`co_viaIfAsync` 内联语义**：RPC 模式下整个物理算子树在 IO 线程上执行，无计算隔离。

5. **GraphTxnHandle 是 `void*`**：指向堆上 `TxnState`，commit/rollback 后释放。任何后续访问是 use-after-free。

6. **CREATE INDEX 同步回填**：当前阻塞用户请求直到回填完成，大表会很慢。
