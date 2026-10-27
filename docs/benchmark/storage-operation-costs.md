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
- `EmptyDispatchVoid`：compute 线程 → 1 个 IO 线程 → 返回，每轮 64 次空 `dispatchVoid`。

本机一次结果（Release，3 次重复，取 mean）：

| 操作 | 平均耗时 |
|---|---:|
| WT `search` 精确命中 | **567 ns** |
| WT `search_near` 前缀命中 | **411 ns** |
| IO `dispatchVoid` 空任务 | **约 162 ns/次**（10.37 µs / 64 次，items/s≈6.19M） |

> 基准的 `Time` 是每轮 64 次 dispatch 的总时间；表中按 `items_per_second` 换算成单次。

## 3. 语句归因（CsvComposite sf0.1，单次 trace）

| 查询 | wt_search | wt_search_near | io_dispatches | seek_est | dispatch_est | wall（约） | 占比 |
|---|---:|---:|---:|---:|---:|---:|---:|
| complex-5 | 29,894 | 122,390 | 106 | 16.9 + 50.3 = **67.2 ms** | 0.02 ms | ~240 ms | **28%** |
| complex-9 | 0 | 107,131 | 473 | **44.0 ms** | 0.08 ms | ~190 ms | **23%** |
| complex-14 | 0 | 163,703 | 91,672 | **67.3 ms** | 14.8 ms | ~291 ms | **28%** |

计算使用：`search=567ns`、`search_near=411ns`、`dispatch=162ns`。

## 4. 结论

- 这三类查询中，**WT seek + dispatch 只能解释约 23–28% 的 wall time**；
- 剩余约 70% 在执行计划、算子循环、Value/列处理、聚合/排序等，继续抠 cursor/dispatch 不会有数量级收益；
- c14 的 `io_dispatches` 高达 **91k**（`getVertexProperties` 60k、`getVertexLabels` 30k），
  结构上确实值得减少；但按空 dispatch 的 162ns 计算只有约 15ms，不能解释 c14 的 291ms；
- 因此下一步优先级应是 **c14 的 PC 去相关/共享展开**、c5 的去相关/半连接，
  而不是继续微调 cursor。

## 5. 口径限制

- seek 计数是逻辑调用次数，不区分缓存命中/缺页；基准是 warm cache，实际查询的 seek 可能更贵；
- dispatch 基准是空任务、1 IO 线程；真实存储调用包含同步工作，且多线程竞争；
- 语句 trace 本身有计数/日志开销，wall time 取自同分支独立 A/B 的近似值。
