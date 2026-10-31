# 指标与诊断日志（op-trace / io-trace / plan debug）

> 面向性能排查与回归定位：怎么打开、日志字段是什么意思、哪些结论不能从这些日志里直接得出。
> 所有开关**默认关闭**，只在排查时短时间打开；不要在生产常开，也不要拿开着 trace 的
> wall time 当基准数字。
>
> 相关实现：
> - `src/query/physical_plan/query_context.hpp` / `physical_operator_base.cpp`（算子 trace）
> - `src/query/executor/query_executor.hpp`（语句结束输出 op/io trace）
> - `src/storage/data/async_graph_data_store.hpp`（IO dispatch / WT seek 计数）
> - `src/query/physical_plan/physical_planner.cpp`、`src/query/optimizer/column_rewrite.cpp`
>   （plan / predicate / DPL debug）

## 1. 环境变量总表

| 变量 | 取值 | 生效范围 | 输出 | 开销/注意 |
|---|---|---|---|---|
| `EUGRAPH_OP_TRACE` | 非空 | 每条语句（`QueryContext` 构造时读取） | 每个已执行物理算子一行 `[op-trace]`，语句结束时输出 | 每个算子多一层协程包装 + 每次 resume 两次时钟读取；大结果集日志很多 |
| `EUGRAPH_IO_TRACE` | 非空 | 每条语句（`AsyncGraphDataStore` 实例） | 语句结束一行 `[io-trace]` | 每次 dispatch/seek 原子计数；日志只在计数非零时输出 |
| `EUGRAPH_PLAN_DEBUG` | 非空 | 规划期 | `[PLAN]` 物理算子树（含 slot layout） | 只在 `planBound` 路径输出；Cascades 优化器路径可能看不到 |
| `EUGRAPH_PRED_DEBUG` | 非空 | 规划期（`planBound` 完成后） | `[PRED]` 每个 Filter 的谓词描述 | 仅 Filter；每个谓词一行 |
| `EUGRAPH_DPL_DEBUG` | 非空 | 规划期 requirements 收集 | `[DPL]` 每个 slot 的物化需求 | 用于解释“为什么整顶点被物化” |
| `EUGRAPH_LJ_BATCH` | 整数 | LeftJoin 批处理 | 无日志（调参） | 默认 512；0 关闭批处理；建议 <1024 |
| `EUGRAPH_NUMA_BIND` | `1` | 线程创建时 | 无日志（行为开关） | 默认关闭；单 NUMA 节点上是 no-op |

> `EUGRAPH_OP_TRACE` / `EUGRAPH_IO_TRACE` 是**语句级**的：同一进程的并发语句各自
> 统计、各自输出；日志会带语句最后的 query 文本（io-trace）或缩进树（op-trace）。

## 2. `EUGRAPH_OP_TRACE`：算子级 rows/chunks 与 CPU/wall

### 2.1 打开与示例

```bash
EUGRAPH_OP_TRACE=1 ./build-release/eugraph-server \
  --data-dir /path/to/graph --bolt-port 7688 > /tmp/server.log 2>&1 &
# 跑一条查询后
grep '\[op-trace\]' /tmp/server.log | tail -40
```

输出是一棵**按缩进表示的算子树**（根在上），例如：

```text
[op-trace] Limit(20) rows=20 chunks=1 self_cpu_ms=1.101 incl_cpu_ms=1.101 self_wall_ms=1.121 incl_wall_ms=273.309
[op-trace]   Project(items=[forumName, postCount]) rows=1024 chunks=1 self_cpu_ms=0.391 incl_cpu_ms=246.144 ...
[op-trace]     Sort(items=2) rows=1024 chunks=1 ...
[op-trace]       ProjectionExtract(specs=[...]) rows=8814 chunks=9 ...
[op-trace]         LeftJoin rows=9017 chunks=18 ...
```

### 2.2 字段

| 字段 | 含义 |
|---|---|
| `<缩进><op>` | 物理算子 `toString()`；缩进每层 2 空格，表示 trace 记录里的父子关系 |
| `rows` | 该算子本次语句**产出的行数之和**（所有 chunk 的 `count` 累加） |
| `chunks` | 产出的 chunk 数 |
| `self_cpu_ms` | 本算子自身 CPU 时间（不含直接子算子）；`incl - Σ(直接子算子 incl)`，小于 0 时截断为 0 |
| `incl_cpu_ms` | 含整个子树的 CPU 时间 |
| `self_wall_ms` / `incl_wall_ms` | 同上，但用 steady_clock；包含调度等待，负载高时明显大于 CPU |

### 2.3 归因语义（重要）

- 日志由 spdlog 输出到 server 的 stdout/日志文件；`[op-trace]` 行本身**不含 query 文本**，
  并发语句要按时间/连接区分（`[io-trace]` 行带 query 文本）。
- 记录在算子**第一次被 resume** 时创建，父记录是当时的 `tls_current_op_trace`。
  generator 在 `co_yield` 前把 TLS 还给调用方，resume 后再装回自己，因此
  “先 pull 左孩子、再 pull 右孩子”的 join 不会把右孩子挂到左孩子名下。
- 只有**真正执行过**的算子有记录；未执行的分支（空输入、短路）不会出现。
- 同一算子实例一条记录；父子关系按首次 pull 的嵌套关系，不是静态计划树。
- `rows=0` 是正常的（例如空结果），不代表没执行。

### 2.4 CPU 时间的边界

- `CLOCK_THREAD_CPUTIME_ID` 是**每线程**时钟；协程跨线程 resume 的区间不计 CPU
  （差值可能为负，会计为 0）。因此 `incl_cpu_ms` 可能小于 `incl_wall_ms`，
  跨线程池的等待只体现在 wall 上。
- trace 本身有开销（每 chunk 的两次 `clock_gettime`、协程包装、日志 I/O）；
  **性能数字必须关闭 trace 测**，trace 数字只用于相对定位。

## 3. `EUGRAPH_IO_TRACE`：语句级 IO dispatch 与 WT seek 计数

### 3.1 打开与示例

```bash
EUGRAPH_IO_TRACE=1 ./build-release/eugraph-server ... > /tmp/server.log 2>&1 &
grep '\[io-trace\]' /tmp/server.log | tail -1
```

```text
[io-trace] io_dispatches=473 wt_search=0 wt_search_near=107131
  scanCursorStream=290, getVertexLabels=174, batchGetVertexCoalesceProperties=4,
  batchGetVertexProperties=2, getVertexLabelsBatch=2 query=...
```

### 3.2 字段

| 字段 | 含义 |
|---|---|
| `io_dispatches` | 本条语句对 `AsyncGraphDataStore::dispatch*` 的调用次数（每次投递到 Storage IO 池算一次） |
| `wt_search` | IO 线程上 WT cursor 的**精确** `search` 次数 |
| `wt_search_near` | WT cursor 的**前缀** `search_near` 次数 |
| `by_call` | dispatch 次数按调用点的 top5（函数名来自 `std::source_location`） |
| `query` | 触发这条统计的语句文本（截断/换行按原样） |

计数是**语句级**的：`AsyncGraphDataStore::for_session` 为每条语句构造实例，语句结束
输出一次；IO 线程上的计数通过原子变量汇总。只有各项都为 0 时不输出。

### 3.3 重要口径：IoScheduler 自 2026-10-31 起真正跨线程

`IoScheduler` 已从 `co_viaIfAsync` 改为 `co_withExecutor`：存储调用在 Storage IO 池执行，
完成后协程恢复到调用方 executor。因此 `io_dispatches` 现在是**真实跨线程 dispatch 次数**，
可按约 10 µs/次作为上界估算（`storage_op_bench` 的 `ProductionDispatchVoid`）；修复前的
inline 路径保留为 `LegacyInlineDispatchVoid` 仅用于同二进制 A/B。`io_dispatches` 高说明
需要批量化，而不是 seek 慢。详见
[storage-operation-costs.md](../benchmark/storage-operation-costs.md) 与
[execution-model.md](../query/engine/execution-model.md) §三。

### 3.4 用计数估算 seek 成本

```text
seek_est = wt_search × T_search + wt_search_near × T_search_near
```

本机 Release、warm cache 的基准值：`T_search ≈ 567 ns`，`T_search_near ≈ 411 ns`
（见 storage-operation-costs.md §6）。这只解释 seek 部分；剩余时间要用 op-trace 或
profile 归因到算子/计划。

## 4. 计划/谓词/需求调试

### 4.1 `EUGRAPH_PLAN_DEBUG=1`：物理计划与 slot layout

```text
[PLAN] Limit(20) |slots: 11 10 |
[PLAN]   Project(items=[forumName, postCount]) |slots: 11 10 |
[PLAN]     Sort(items=2) |slots: 4 2147483654 10 |
```

- 缩进是静态算子树；`|slots: ... |` 是每个输出列的 `SlotId`（`TupleSlotLayout`）。
- 输出点在 `PhysicalPlanner::planBound`；走 Cascades 选择路径的语句可能**没有** `[PLAN]`
  行（不要据此判断计划为空）。
- 可与 `[op-trace]` 对照：计划里的算子顺序 + trace 的实际执行时间。

### 4.2 `EUGRAPH_PRED_DEBUG=1`：Filter 谓词

```text
[PRED] filter pred=Binary(15, ColRef(friend slot=2147483653 kind=4), ColRef(friends slot=5 kind=10))
```

`describeBoundExpression` 的简写：

| 片段 | 含义 |
|---|---|
| `ColRef(name slot=N kind=K)` | 列引用；`kind` 是 `BoundTypeKind` 的数值（如 4=VERTEX，10=LIST） |
| `PropRef(prop cands=N obj=...)` | 属性引用；`cands` 是候选 (label, prop) 数 |
| `Binary(op, left, right)` | 二元表达式；`op` 是 `cypher::BinaryOperator` 数值 |
| `Func(name)` | 函数调用（参数不展开） |
| `Literal` | 字面量 |

同样只在 `planBound` 路径输出；Cascades 路径可能没有。

### 4.3 `EUGRAPH_DPL_DEBUG=1`：Demand-Pull 需求

```text
[DPL] slot=123 var=friend whole_vertex=0 whole_edge=0 labels=0 edge_type=0 vprops=1 eprops=0 coalesce=1
```

用于回答“为什么这个变量被当成整顶点/整边物化，而不是只读需要的属性”：

| 字段 | 含义 |
|---|---|
| `slot` / `var` | 绑定槽与变量名（`<anon>` 表示匿名） |
| `whole_vertex` / `whole_edge` | 下游是否要求整个 VERTEX / EDGE 对象 |
| `labels` / `edge_type` | 是否要求标签列表 / 边类型 |
| `vprops` / `eprops` | 顶点/边属性需求数 |
| `coalesce` | 多候选 label 的 coalesce 属性组数 |

## 5. 调参开关（不是指标）

### 5.1 `EUGRAPH_LJ_BATCH`

`LeftJoinPhysicalOp` 的 correlated 右子计划按批执行，默认 **512** 行/批：

- `EUGRAPH_LJ_BATCH=0`：退化为逐行执行（每行都重跑右子计划）；
- 非 0：按该批大小执行；**必须 < 1024**（`DataChunk::DEFAULT_CAPACITY`），
  跨 chunk 容量边界会改变下游批行为（历史上有 postCount 丢行的复现）；
- 右子计划输出含 `PATH` / `PATH_TOPOLOGY` 时自动回退逐行（批处理会破坏命名路径）。

该开关只用于定位批处理相关问题，不作为常规调优参数。

### 5.2 `EUGRAPH_NUMA_BIND=1`

线程创建时按 NUMA 节点 round-robin 绑定（节点粒度，不是绑核），默认关闭；
单节点机器上是 no-op。用于确认跨 socket 访问是否是瓶颈，不要默认开启。

## 6. 指标 API（代码内使用）

`AsyncGraphDataStore` 每条语句暴露：

| 方法 | 含义 |
|---|---|
| `ioDispatches()` | 本语句 dispatch 次数 |
| `wtSearchCount()` | 本语句 WT `search` 次数 |
| `wtSearchNearCount()` | 本语句 WT `search_near` 次数 |
| `ioDispatchSummary()` | 按调用点聚合的 top5 dispatch 明细 |

底层 `StorageOpCounters` 由 sync store 的 cursor 在 IO 线程累加；单元测试
`tests/test_storage_op_metrics.cpp`，微基准 `benchmark/storage/storage_op_bench.cpp`
（`storage_op_bench`，需 `-DEUGRAPH_ENABLE_BENCHMARK=ON`）。

## 7. 典型用法

### 7.1 单条查询拆时间

```bash
EUGRAPH_OP_TRACE=1 EUGRAPH_IO_TRACE=1 ./build-release/eugraph-server \
  --data-dir /path/to/graph --bolt-port 17688 > /tmp/trace.log 2>&1 &
python3 scripts/bench_ldbc_interactive.py --queries-dir <queries> \
  --uri bolt://127.0.0.1:17688 --only complex-5 --warmup 0 --iters 1
grep '\[op-trace\]' /tmp/trace.log | tail -30
grep '\[io-trace\]' /tmp/trace.log | tail -1
```

先看 `[op-trace]` 里 `self_cpu_ms` 最大的算子，再看 `[io-trace]` 的 seek 次数是否
与之匹配；两者不一致时用 GDB/perf 采样（见
[query-performance-optimization-notes.md](../benchmark/query-performance-optimization-notes.md) §2）。

### 7.2 对照计划

```bash
EUGRAPH_PLAN_DEBUG=1 EUGRAPH_PRED_DEBUG=1 ./build-release/eugraph-server ... 
# 触发一次规划后
grep '\[PLAN\]' /tmp/server.log | tail -60
grep '\[PRED\]' /tmp/server.log | tail -10
```

### 7.3 解释整对象物化

```bash
EUGRAPH_DPL_DEBUG=1 ./build-release/eugraph-server ...
grep '\[DPL\]' /tmp/server.log | grep 'whole_vertex=1'
```

## 8. 限制与注意事项

1. **日志量**：trace 面向单条/少量查询。对循环查询开 `EUGRAPH_OP_TRACE=1` 曾把
   日志写到 5.5 GB 并把 `/tmp` 写满；复现时用 `--iters 1`、单查询、日志重定向到
   磁盘并加轮转/清理。
2. **trace 开销**：op-trace 每算子多一层协程 + 每 chunk 时钟读取；io-trace 每
   dispatch/seek 原子计数。两者都会改变 wall time，不能与关闭 trace 的数字直接比。
3. **CPU 不是 profile**：op-trace 的 CPU 是线程 CPU 时间差，跨线程区间被跳过；
   要函数级热点仍需 perf/GDB 采样。
4. **io_dispatches 不是跨线程次数**：当前 IoScheduler inline；不要乘 10 µs。
5. **plan/pred 只覆盖 `planBound`**：Cascades 路径可能没有 `[PLAN]`/`[PRED]`；
   此时以 op-trace 的算子名为准。
6. **绝对时间受频率影响**：同二进制交错 A/B，至少 3 轮，min 与 p50 都记录；
   CPU 降频时同一二进制可慢约 2×。

## 9. 相关文档

- [执行模型：协程/流式与 IO 调度](../query/engine/execution-model.md)
- [Storage 操作成本与 seek/dispatch 基准](../benchmark/storage-operation-costs.md)
- [查询性能排查与优化笔记（GDB 采样、A/B 纪律）](../benchmark/query-performance-optimization-notes.md)
- [ProjectionExtract 按需物化与 slot 体系](../query/engine/projection-extract-design.md)
- [构建指南](../build/build-guide.md)（ASan/UBSan）
