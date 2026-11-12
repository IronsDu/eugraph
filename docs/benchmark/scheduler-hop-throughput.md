# 双调度器 hop 的并发吞吐（单 worker）

> 目标：回答“计算 → Storage IO → 计算”的跨调度器开销，在并发数增加时是否会被摊薄。
> 这个 benchmark 刻意让两个 executor **各只有 1 个 worker**，排除池内多线程并行，
> 只观察固定 hop 成本、队列和流水线重叠。
>
> 源码：[benchmark/scheduler/executor_hop_bench.cpp](../../benchmark/scheduler/executor_hop_bench.cpp)


> **注意（2026-11-12 补充）**：本 benchmark 的 compute/IO work 都是**自旋、无访存**，
> 因此只能测 enqueue/wakeup 的固定成本。真实查询里 hop 的更大代价是**数据跨核**：
> 存储调用在 IO 线程的核上把数据读进 cache，consume 它的 compute 线程在另一个核上，
> 同一份数据要走两遍 cache。SF1 c7 上用 `perf stat` 实测：hop 模式 cache-miss/op
> 206–272k，存储调用 inline 到 compute 线程后 143–167k（−25~45%），IPC 2.10–2.31 → 2.35–2.37。
> 详见 [ldbc-snb-concurrency.md §8](ldbc-snb-concurrency.md)。

## 形状

```text
compute(1) --dispatch--> IO(1) --resume--> compute(1)
```

每条 request 循环 `hops` 次：

1. 在 compute 上做 `compute_work` 次自旋；
2. `co_await io.dispatch(...)`，IO 线程做 `io_work × batch` 次自旋；
3. 回到 compute 再做 `compute_work` 次自旋；
4. 记录一次完整 round-trip 延迟。

同时启动 `N` 条 request，全部完成后统计 wall time、hop/s、ops/s、p50/p99。
`batch` 表示一次 dispatch 携带的 IO 工作单元数，用于区分“并发”与“批量化”。

## 命令

```bash
cmake -S . -B build-bench -DEUGRAPH_ENABLE_BENCHMARK=ON
cmake --build build-bench --target executor_hop_bench -j2
./build-bench/benchmark/scheduler/executor_hop_bench
./build-bench/benchmark/scheduler/executor_hop_bench --compute-work 0 --io-work 0
./build-bench/benchmark/scheduler/executor_hop_bench --batch 16 --io-work 125
```

默认：`compute_work=2000`、`io_work=2000`、`hops=2000`、`batch=1`、
并发 `1,2,4,8,16,32,64,128`。

## 结果（本机 2026-11-01）

### batch=1，每个 hop 至少 1 个 IO 单元

| 并发 | hops/s | ops/s | p50 (µs) | p99 (µs) |
|---:|---:|---:|---:|---:|
| 1 | 59,740 | 59,740 | 15.5 | 28.2 |
| 2 | 153,878 | 153,878 | 12.5 | 24.0 |
| 4 | 191,961 | 191,961 | 20.3 | 30.2 |
| 8 | 197,784 | 197,784 | 40.0 | 52.3 |
| 16 | 197,948 | 197,948 | 80.0 | 107.1 |
| 32 | 193,723 | 193,723 | 162.5 | 214.9 |
| 64 | 197,148 | 197,148 | 323.3 | 371.2 |
| 128 | 197,422 | 197,422 | 646.4 | 689.1 |

### 纯 hop：`compute-work=0, io-work=0`

这是最接近“只测入队/唤醒/切换”的一组：request 里除了 `dispatch/resume` 没有自旋工作。

| 并发 | hops/s | p50 (µs) | p99 (µs) |
|---:|---:|---:|---:|
| 1 | 102,015 | 9.1 | 17.1 |
| 2 | 191,342 | 9.7 | 20.1 |
| 4 | 656,024 | 4.9 | 17.3 |
| 8 | 834,322 | 8.5 | 22.9 |
| 16 | 856,901 | 16.7 | 32.1 |
| 32 | 995,344 | 31.5 | 46.1 |
| 64 | 999,832 | 63.1 | 84.4 |
| 128 | 993,334 | 126.8 | 158.4 |

这张表直接回答了“固定成本是否随并发线性增长”：

- `N=1` 时每次 round trip 约 **9.8 µs**（约 102k hops/s），包含一次 IO 唤醒 + 两次调度；
- `N=2` 吞吐翻倍，`N=4` 到 656k（约 6.4×），`N=64` 约 1.0M；
- 之后吞吐稳定在 ~1M，继续加并发只增加排队：`N=64` p50 63 µs，`N=128` p50 127 µs。
- `N=4` 的 p50 反而比 `N=1` 低（4.9 µs vs 9.1 µs）：池不再频繁 idle→wake，唤醒成本被
  多个 hop 共享；这是“唤醒/OS 切换子线性”的直接体现。
- 但吞吐平台说明**每任务残余成本**仍在（入队、协程恢复、cache 失温、队列原子操作）；
  平台约 1M hops/s / 2 个 worker ≈ 每个 hop 2 µs 的 CPU 预算。

### batch=16，每 hop 的 IO 总工作保持不变（`io-work=125`）

| 并发 | hops/s | ops/s | p50 (µs) | p99 (µs) |
|---:|---:|---:|---:|---:|
| 1 | 60,789 | 972,629 | 15.5 | 26.7 |
| 2 | 155,775 | 2,492,395 | 12.1 | 24.3 |
| 4 | 201,579 | 3,225,269 | 19.5 | 26.4 |
| 8 | 204,656 | 3,274,489 | 38.6 | 47.5 |
| 16 | 199,094 | 3,185,508 | 78.0 | 113.3 |
| 32 | 198,282 | 3,172,515 | 157.2 | 216.5 |
| 64 | 201,804 | 3,228,872 | 315.6 | 348.8 |
| 128 | 197,633 | 3,162,133 | 637.9 | 763.7 |

## 解读

1. **并发能从空池的固定成本中救出吞吐，但很快到平台。**
   `N=1` 时只有 59.7k hops/s，`N=2` 升到 153.9k，`N=4` 约 192k，此后基本稳定在
   ~197k。原因是两个池各 1 个 worker，compute 和 IO 可以流水线重叠，但单 worker
   的串行服务能力是硬上限；`N≥8` 增加的主要是排队，`p99` 和排队延迟随 N 增长。
2. **吞吐平台内，延迟随并发近似线性增长。**
   `N=128` 时 p50 646 µs，`128 / 197k ≈ 650 µs`，符合 Little's law；这不是每个 hop
   变慢了，而是队列变长了。
3. **真正的摊薄来自 batch，不是并发。**
   batch=16 时 hops/s 仍是 ~200k（每个 hop 的固定调度成本没变），但 ops/s 从 ~200k
   升到 ~3.2M，正好约 16×。并发只是让两个单 worker 的流水线填满；一次 dispatch
   携带更多工作才降低单位操作的 hop 成本。
4. **唤醒/上下文切换不是严格随 N 线性增加，但每任务残余成本仍在。**
   队列已热时不需要每任务唤醒，worker 也通常连续取队列任务而不做 OS 上下文切换；
   但入队/出队、协程恢复、cache 失温、队列竞争仍是每任务成本。两个池各 1 worker 时，
   这部分由 compute/IO 的串行服务能力封顶，所以吞吐会平台化。

> 这是合成自旋负载，不是 WT/真实查询的容量数字；它只用于分离“调度 hop”与
> “有用工作”，展示并发、排队、批量化三者的区别。
