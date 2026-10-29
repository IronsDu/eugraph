# Storage 操作成本：WT seek 与 IO dispatch

> 环境变量、日志字段与限制统一见
> [指标与诊断日志](../debugging/metrics-and-tracing.md)。

目标：把一条语句的成本拆成两个可测量项：

```text
seek_est     = wt_search × T_search + wt_search_near × T_search_near
dispatch_est = io_dispatches × T_dispatch
```

再与语句 wall time 对比，判断瓶颈是 WT seek、线程调度，还是计划/算子。

## 1. 语句级计数

`AsyncGraphDataStore` 在每次 IO dispatch 的 lambda 上安装线程局部计数器，
因此 sync store 里的 `cursor->search` / `cursor->search_near` 能按**当前语句**累计，
并跨多个 IO 线程汇总（原子计数）。

开启：

```bash
EUGRAPH_IO_TRACE=1 ./eugraph-server ...
```

语句结束日志：

```text
[io-trace] io_dispatches=<N> wt_search=<N> wt_search_near=<N> by_call=<top5> query=<text>
```

默认关闭；关闭时不安装 scope，不做计数。

## 2. 微基准

构建（不默认进 ctest）：

```bash
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release \
  -DEUGRAPH_ENABLE_BENCHMARK=ON
cmake --build build-bench --target storage_op_bench -j2
./build-bench/benchmark/storage/storage_op_bench \
  --benchmark_min_time=0.2s --benchmark_repetitions=3
```

基准内容：

- `SearchExact`：100k 行 `edge_index` 形状的 key，warm cache，单 cursor 复用，精确 `search`；
- `SearchNearPrefix`：同一张表，8B 前缀 `search_near`；
- `InlineDispatchVoid`：当前生产 `IoScheduler::dispatchVoid` 路径；
- `CrossThreadDispatchVoid`：独立地按“compute → IO → compute”实现的一次真实跨线程往返，
  同时输出 `cross_thread`（lambda 是否在 IO 线程）和 `return_thread`（返回后是否仍在 IO 线程）。
  后者必须为 0。

本机一次结果（Release，3 次重复，取 mean）：

| 操作 | 平均耗时 |
|---|---:|
| WT `search` 精确命中 | **567 ns** |
| WT `search_near` 前缀命中 | **411 ns** |
| `InlineDispatchVoid`（当前生产路径） | **约 0.3–0.5 µs/次** |
| `CrossThreadDispatchVoid`（真实跨线程） | **约 10.0 µs/次**（641 µs / 64 次） |

`CrossThreadDispatchVoid` 的 `cross_thread` 覆盖全部 640k 次操作，`return_thread=0`：
确实到了 IO 线程，并且完成后回到 compute 线程。

## 3. 重要发现与当前取舍

### 3.1 旧 IoScheduler 是 inline

旧实现：

```cpp
auto ioTask = co_invoke([f]() -> Task<R> { co_return f(); });
co_await co_viaIfAsync(io_ka_, std::move(ioTask));
```

`co_invoke` 返回 eager Task，`f()` 在 `co_viaIfAsync` 之前就在调用线程执行。因此
`io_dispatches` 统计的是调用次数，旧路径并不是线程切换；`InlineDispatchVoid` 的
`cross_thread=0` 验证了这一点。

### 3.2 真正跨线程会破坏写入路径

临时改成 `co_withExecutor(io_ka_, ...)`（不再手动 `co_viaIfAsync` 回 caller executor；
`co_withExecutor` 的 await 会恢复到原执行流）后，`CrossThreadDispatchVoid` 全部跨线程、
`return_thread=0`；c14 的 per-endpoint 读取也从 1.4 s 降到 154 ms。但全量 TCK 在
`CREATE` 场景（MatchWhere1）挂起：
WT session/cursor 绑定在 compute 线程，直接拿到 IO 线程使用会超时/死锁。

因此当前生产 `IoScheduler` **仍保持 inline**；真正的跨线程修复需要先把 WT session
按 IO 线程绑定（或写入路径不跨线程），这属于独立改造。

### 3.3 先做批量读取：c14 已从 291 ms 降到 119.5 ms

`PathElementPropertyRead` 和属性求值改用 `batchGetVertexData`，把
`getVertexLabels` + 每 label 的 `getVertexProperties` 合并为每 chunk 一次批量调用。

| 查询 | wt_search | wt_search_near | io_dispatches | seek_est | wall（约） | seek 占比 |
|---|---:|---:|---:|---:|---:|---:|
| complex-5 | 29,894 | 122,390 | 106 | **67.2 ms** | ~254 ms | **26%** |
| complex-9 | 0 | 107,131 | 473 | **44.0 ms** | ~191 ms | **23%** |
| complex-14（批量后） | 0 | 72,727 | **696** | **29.9 ms** | ~119.5 ms | **25%** |

c14 批量前是 163,703 次 search_near、91,672 次 dispatch、wall ~291 ms；
批量后 dispatch 降到 696，wall 约 **119.5 ms**（inline 路径）。

## 4. 结论

- **WT seek 很快**：精确 search 567 ns、前缀 search_near 411 ns；
- 真实跨线程 dispatch 约 **10 µs**，比 seek 贵一个数量级；
- 当前生产仍是 inline，所以 `io_dispatches` 不能直接乘 10 µs；
- c5/c9 的 seek 约占 23–26%，c14 批量后约 23%；剩余主要在执行计划/算子；
- c14 的 91k per-endpoint 读取是真正的大头，批量后已消除，**不需要先修调度器**；
- 下一步：
  1. 为写入路径/ WT session 做 IO 线程绑定，再启用真正跨线程；
  2. c14 PC 去相关/共享展开、c5 去相关/半连接。

## 5. 口径限制

- seek 计数是逻辑调用次数，不区分缓存命中/缺页；基准是 warm cache；
- 跨线程 benchmark 是独立实现，不代表当前生产 inline 路径；
- 语句 trace 本身有计数/日志开销，wall time 取自同分支独立 A/B 的近似值。

## 6. Google Benchmark 汇总（2026-10-28）

构建/运行：

```bash
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DEUGRAPH_ENABLE_BENCHMARK=ON
cmake --build build-bench --target storage_op_bench -j2
./build-bench/benchmark/storage/storage_op_bench \
  --benchmark_min_time=0.3s --benchmark_repetitions=3
```

| Benchmark | 操作 | 平均耗时 |
|---|---|---:|
| `WtSeekBench/SearchExact` | WT 精确 `search` | **567 ns** |
| `WtSeekBench/SearchNearPrefix` | WT 前缀 `search_near` | **411 ns** |
| `KeyDecode_LabelForward` | 8B label-forward key | 8.0 ns |
| `KeyDecode_VertexExistence` | 8B vertex-existence key | 8.0 ns |
| `KeyDecode_VertexDataLabel` | 11B `{vid}{kind}{label}` | 7.4 ns |
| `KeyDecode_VertexDataProp` | 13B `{vid}{kind}{label}{prop}` | 6.9 ns |
| `KeyDecode_VertexDataAdjEdge` | 27B `{vid}{kind}{label}{neighbor}{seq}` | 18.7 ns |
| `KeyDecode_EdgeIndex` | 27B edge-index key | 18.6 ns |
| `KeyDecode_EdgeType` | 24B etype key | 18.6 ns |
| `KeyDecode_EProp` | 10B `{edge}{prop}` | 7.8 ns |
| `KeyDecode_IndexSuffix` | index key 末尾 entity id | 14.1 ns |
| `DispatchBench/InlineDispatchVoid` | 当前 inline dispatch | 0.3–0.5 µs |
| `DispatchBench/CrossThreadCoroutineDispatch` | 真实 compute→IO→compute | **10.1 µs** |

结论：**key decode 本身是 7–19 ns 量级，远低于一次 WT seek（400–600 ns），更远低于一次真实跨线程 dispatch（~10 µs）**。
后续优化不应再以“key 解码”为主要方向；瓶颈在搜索次数、dispatch 次数、计划/算子。
