# Storage 操作成本：WT seek 与 IO dispatch

目标：把一条语句的成本拆成两个可测量项，而不是继续凭感觉优化：

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
- `EmptyDispatchVoid`：调用线程 → `IoScheduler::dispatchVoid` → 返回，每轮 64 次空任务。
  基准同时输出 `cross_thread` 计数：当前实现为 **0**，见下方“dispatch 发现”。

本机一次结果（Release，3 次重复，取 mean）：

| 操作 | 平均耗时 |
|---|---:|
| WT `search` 精确命中 | **567 ns** |
| WT `search_near` 前缀命中 | **411 ns** |
| IO `dispatchVoid` 空任务 | **约 160–220 ns/次（inline，不跨线程）** |

> 基准的 `Time` 是每轮 64 次 dispatch 的总时间；表中按 `items_per_second` 换算成单次。
> `cross_thread=0` 表示当前 `dispatchVoid` 的 lambda 仍在调用线程执行，不是线程切换成本。

## 2.1 重要发现：当前 dispatch 没有跨线程

`IoScheduler::dispatch` 的实现是：

```cpp
auto ioTask = co_invoke([f]() -> Task<R> { co_return f(); });
co_await co_viaIfAsync(io_ka_, std::move(ioTask));
```

`co_invoke` 返回的是 **eager Task**，`f()` 在 `co_viaIfAsync` 之前就已经 inline 执行；
benchmark 的 `cross_thread` 计数实测为 **0**。因此：

- `io_dispatches` 目前统计的是“调用次数”，**不是跨线程调度次数**；
- 之前用 162 ns × dispatch 次数做的归因无效；
- 临时改成 `co_withExecutor` 后，`cross_thread` 变为全部命中，但单次空 dispatch 变成
  **约 5–10 µs**。按 c14 的 91,672 次计算，真实跨线程成本会是 **0.5–0.9 s**，
  大于当前 c14 的 291 ms。这也从侧面证明当前路径确实是 inline 执行。

## 3. 语句归因（CsvComposite sf0.1，单次 trace）

| 查询 | wt_search | wt_search_near | io_dispatches | seek_est | dispatch_est | wall（约） | 占比 |
|---|---:|---:|---:|---:|---:|---:|---:|
| complex-5 | 29,894 | 122,390 | 106 | **67.2 ms** | inline，不计 | ~240 ms | **28%** |
| complex-9 | 0 | 107,131 | 473 | **44.0 ms** | inline，不计 | ~190 ms | **23%** |
| complex-14 | 0 | 163,703 | 91,672 | **67.3 ms** | inline，不计 | ~291 ms | **23%** |

计算使用：`search=567ns`、`search_near=411ns`。dispatch 当前是 inline，不能按跨线程成本计入。

## 4. 结论

- **WT seek 只能解释约 23–28% 的 wall time**；
- dispatch 当前是 inline，`io_dispatches` 不是线程切换成本；真正改成跨线程后，
  c14 的 91k 次会达到 0.5–0.9 s，必须先做 dispatch 批量化/去重，不能直接修调度器；
- 剩余约 70% 在执行计划、算子循环、Value/列处理、聚合/排序等；
- 因此下一步优先级应是：
  1. **先确认并修复 IoScheduler 的 inline 问题**（需配合 dispatch 批量化，否则 c14 回退）；
  2. **c14 的 PC 去相关/共享展开**、c5 的去相关/半连接。
  不能再用当前 `io_dispatches × 162ns` 做调度成本归因。

## 5. 口径限制

- seek 计数是逻辑调用次数，不区分缓存命中/缺页；基准是 warm cache，实际查询的 seek 可能更贵；
- dispatch 基准是空任务、1 IO 线程；真实存储调用包含同步工作，且多线程竞争；
- 语句 trace 本身有计数/日志开销，wall time 取自同分支独立 A/B 的近似值。
