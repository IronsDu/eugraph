# SF1 并发吞吐（官方 LDBC driver，4 并发）

> 2026-11-12，Release 构建（main `66e9156b` + 本分支），官方 LDBC driver
> （`ldbc_snb_interactive_v1_impls/cypher/driver`），`--compute-threads 8 /
> --storage-io-threads 4 / 4 GB WT cache / --wt-txn-sync none`，SF1 数据
> `bench-sf1-latest`（14 GB 内存的机器，本组测量期间 swap 已用 6 GB）。
>
> 结论先说：**存储点读和 compute→IO 调度 hop 本身都能随并发线性扩展
> （3.4–3.8× / 6.6×），端到端查询只有 2.1–3.0×**。没有找到单一串行锁；
> 天花板来自「一条查询要占两个线程（compute + storage IO）」+ 并发下每次操作
> CPU 上升 ~22% + 大查询并发时的内存/缓存压力。

## 1. 口径与坑

driver 用法（Java 25 需要 `--add-exports`，否则 disruptor 直接 `IllegalAccessError`）：

```bash
java --add-exports java.base/sun.nio.ch=ALL-UNNAMED \
     --add-opens java.base/java.lang=ALL-UNNAMED \
     --add-opens java.base/java.nio=ALL-UNNAMED \
     -cp target/cypher-1.2.0-SNAPSHOT.jar org.ldbcouncil.snb.driver.Client -P <props>
```

本次测量相对官方 audit 的偏离（都是刻意的）：

- **只读 profile**：8 个 update 关闭，避免把 SF1 数据写脏；官方 audit 的混合写负载未测。
- **`ignore_scheduled_start_times=true`**：背靠背压满，测「最大吞吐」，不做 schedule audit。
- **参数重生成**：driver 自带 `test-data/substitution_parameters` 里的 person id
  （如 `4398046511333`）在本机这份 SF1 导入里**不存在**（Person.id 是
  65…35184372099695 的稀疏集合）。用原参数跑出来的 c5/c9 只要 1–2 ms（空结果），
  是假吞吐。本次用 **43 个真实存在的 Person id**（933/1129/4139/143 + 均匀采样）
  重生成参数。
- **c1 关闭**：c1 在 personId=933 时返回 `[null,null,null]` 组织元组，driver 的
  `CypherConverter.asOrganization` 直接 NPE 并中断整个 run（文档已有记录的 driver
  适配问题）。
- short query 的参数由 driver 从 update stream 派生，无法替换；实测 3–20 ms，
  落在真实 person 上（不是空查）。

## 2. 端到端结果

| profile | 线程 | eugraph | neo4j | 差距 |
|---|---:|---:|---:|---:|
| complex-only（600 ops） | 1 | **1.93 op/s** | 4.38 op/s | 2.3× |
| complex-only（600 ops） | 4 | **4.06 op/s** | 16.83 op/s | 4.1× |
| 混合 short+complex（2000 ops） | 4 | **36.23 op/s** | 157.21 op/s | 4.3× |

扩展比（4t/1t）：eugraph **2.10×**，neo4j **3.84×**。
单条延迟膨胀（4t/1t 均值）：eugraph 519 → 981 ms（1.89×），neo4j 228 → 238 ms（1.04×）。

用 Python 客户端复测同一查询（c7，personId=933，warm）得到同样的量级：

| clients | eugraph qps | p50 延迟 | 服务端 CPU |
|---:|---:|---:|---:|
| 1 | 137.5 | 4.1 ms | 0.95 核 |
| 4 | 409.2（**2.98×**） | 7.3 ms | 3.44 核（21% of 16 vCPU） |

即 4 并发时整机仍 **~79% 空闲**、`vmstat` 的 `bi/bo≈0`（无磁盘压力）、`wa=0`。

## 3. 排除掉的串行点

| 嫌疑 | 方法 | 结果 |
|---|---|---|
| 存储点读（WT 单连接多 session） | 新增 `eugraph_tests --gtest_filter='*StorageOperationScaling*'`：1 vs 4 线程、每 worker 一个会话 + 读事务，覆盖 labels/props/coalesce/edge-scan | **3.4–3.8×**（labels 3.4–3.8×、props 3.4×、coalesce 3.5×、scanBatch 3.4–3.8×） |
| compute→IO→compute hop | 新增 `io_scheduler_tests --gtest_filter='*DispatchHopScaling*'` | 1 worker 15 µs/hop，4 worker 8 µs/hop；吞吐 67k → 497k hops/s（**没有退化**） |
| SMT/线程摆放（compute 与 IO 线程共享物理核） | 用 `taskset` 把 compute 钉到 CPU 0/2/4/6、IO 钉到 8/10/12/14（互不共享物理核） | 4 并发 403 → 409 qps（**+1.5%，无改善**） |
| CPU 饱和 | 服务端 CPU 采样 | 4 并发 3.44 核 / 16 vCPU |
| 磁盘 | `vmstat` 的 `bi/bo/wa` | ≈0 / 0% |
| 驱动/客户端 | 同一 Java driver 打 neo4j 为 3.84×；Python 客户端复现 2.98× | 不是 Java driver 的线程模型 |

## 4. 剩下的天花板（有数据）

- **每条查询占两个线程**：compute 线程跑算子，storage IO 线程跑 WT 调用，中间是
  `compute→IO→compute` hop（~8–15 µs/次，见上表）。4 并发 = 8 个线程，在 8 物理核
  （16 逻辑）的机器上刚好压满一半 SMT；neo4j 每条查询单线程，4 并发只占 4 核，
  所以它更容易接近线性。
- **并发下每操作 CPU +22%**：Python c7，1 client 6.9 ms CPU/op，4 clients 8.4 ms CPU/op
  （服务端 0.95 → 3.44 核，吞吐只 2.98×）。这部分增量分摊在所有线程上，没有单点阻塞，
  与内存分配器/缓存争用一致。
- **大查询并发时受内存/缓存压力**：Java driver 的 complex-only mix（c5 2.8 s、c9 1.5 s、
  c3 1.3 s、c14 0.7 s 同时跑）只拿到 2.10×，而轻量 c7 是 2.98×。SF1 工作集 4.5 GB、
  WT cache 4 GB、整机 14 GB 且 swap 已用 6 GB——4 条重查询同时跑时页面压力明显。
- 由此，**下一层优化应按「减少每条查询的 IO 往返/线程占用」走**（批量点读、合并属性读、
  更大 chunk），而不是找锁。逐条 PROFILE 的 `totalIoDispatches` 显示 c5 9,340、
  c14 5,460、c12 2,823，而 c9/c3 只有 71/388——dispatch 次数与「每次 dispatch 里做的
  点读数量」差异巨大，批量化空间仍在。

## 5. 复现命令

```bash
# 服务端
./build/release/eugraph-server -d /home/dodo/code/fuck/bench-sf1-latest \
  --bolt-port 7688 --thrift-port 9190 --web-port 0 \
  --compute-threads 8 --storage-io-threads 4 --wt-cache-size-mb 4096 --wt-txn-sync none
# 诊断单测
./build/release/eugraph_tests --gtest_filter='*StorageOperationScaling*'
./build/release/io_scheduler_tests --gtest_filter='*DispatchHopScaling*'
# driver（properties 见 /tmp/bench-4c-*.properties 的构造方式）
```

## 6. 顺带发现：PROFILE + 并发会崩服务端（**既存缺陷**）

3 条连接循环 c7 + 1 条连接循环 `PROFILE c7`，**main 基线二进制**和本分支构建都会在
数秒内崩溃：基线是 SIGSEGV（栈顶 `pinStorageOpCounters` ← `AsyncGraphDataStore::beginSession`），
本分支构建是 `free(): chunks in smallbin corrupted`（Bolt PackStream 侧）。
不带 PROFILE 的 4 连接普通负载跑 150–180 s 不崩。根因指向
`StorageOpCounters` 的 TLS 裸指针在语句结束后悬垂（`weak_from_this()` on freed object），
只有 PROFILE 会安装 per-statement 计数器。**与本分支优化无关**，已单独开 Issue #310。

## 7. 追加实验（2026-11-12 下午）：4 并发上限到底卡在哪

结论：**没有单点锁**；上限由「每次操作的 CPU 上升 + 一条语句只占两个线程的串行流水线」共同决定，
其中一部分来自这台 14 GB 笔记本（4 线程时最高频率从 4.19 GHz 降到 3.59 GHz、L3 共享、
Chrome/GNOME 等后台约占 0.4 核）。

| 实验 | 结果 | 结论 |
|---|---|---|
| `--bolt-io-threads 1 → 4` | c7 4 客户端 496 → 504 qps（±2%）；1 客户端 184 → 205 | Bolt IO 线程不是瓶颈（CPU 占用 <1%） |
| `--storage-io-threads 4 → 8` | c7 4 客户端 496 → 530 qps（±3%） | IO 池大小不是瓶颈 |
| compute/IO 线程钉到互不共享物理核（`taskset`） | c7 403 → 409 qps；纯计算查询 223.3 → 222.9 qps | 线程摆放不是原因 |
| 同一查询 vs 不同 person（4 客户端） | 358.4 vs 358.4 qps（完全一致） | 不是热点页/hazard 竞争 |
| 纯计算查询（`UNWIND range(1,200000)`，无存储） | 1 客户端 72.9 qps → 4 客户端 192.0 qps（**2.63×**）；p50 13.5 → 18.0 ms | **与存储无关的 CPU 侧本身就只有 2.6×** |
| 同上，`LD_PRELOAD=libtcmalloc_minimal` | 1 → 4 客户端 73.9 → **223.3 qps（3.02×）**；c7 140 → **443.7 qps（3.17× vs 2.88×）** | glibc malloc 在 4 并发下白吃 ~16% 吞吐 |
| 同上，CPU 频率采样 | 1 客户端 busy 核 mean 3675 / max 4192 MHz；4 客户端 mean 3372 / max 3593 MHz | 4 并发时最高频率 **−14%**，同一份指令的 CPU 时间同比例变长 |
| 重查询 c9（1.05M `wtSearchNear` + 919k scanRow/op） | 1 客户端 0.6–0.7 qps → 4 客户端 1.1–1.4 qps（**约 2×**）；CPU/op 1.33 s → 1.98 s（**+49%**）；同期 `bi≈400` blocks/s、`wa=0` | 重查询的上限是缓存/内存带宽，不是锁、也不是磁盘 |
| 存储微基准：10k 顶点（L3 内） | labels/props/coalesce/edges **3.7–4.25×**，CPU/unit 基本持平 | 存储层本身可扩展 |
| 存储微基准：300k 顶点（**超出 L3**） | labels 4.85×、coalesce 3.72×，CPU/unit 持平 | 点读在“非缓存”规模下也接近线性 |

**综合解释**：

1. 我们的每条语句占 **两个线程**（compute + storage IO）且是**依赖流水线**：compute 要等 IO
   返回才能继续。4 并发 = 8 个忙碌线程，正好压到 8 物理核/16 逻辑核的 SMT 与频率边界；
   neo4j 每条语句 1 个线程、存储内联，4 并发只占 4 核，所以它能到 3.84×。
2. **每次操作的 CPU 在并发下上升 23–49%**：纯计算查询是 +30%（其中 −14% 来自频率，
   其余是 L3/内存与分配器），重查询 c9 是 +49%（4 路各扫 ~1M 行，内存带宽/L3 压力）。
3. glibc malloc 在 4 并发下额外吃掉约 16% 吞吐（tcmalloc 对照实验）；这是可以靠
   “减少每次操作的分配/拷贝”拿回来的一部分。
4. **没有发现单点串行资源**：存储点读、dispatch hop、Bolt IO、IO/计算线程池、线程摆放、
   热点页共享都被实测排除。

**可落地的方向（按性价比）**：

- 减少每操作的分配/拷贝（tcmalloc 实验给出的上界 ~16%）：chunk/Value 复用、少一次深拷贝；
- 减少每语句的 hop 数并让存储调用**重叠**（当前一条语句同时只能有一个 IO 调用在飞），
  这是结构性改动，能把「compute+IO 串行」变成流水线；
- 机器/缓存：4 GB WT cache 对 4.5 GB 数据、14 GB 内存的笔记本偏紧，重查询混合时更明显；
  这不是引擎缺陷，但会放大 4 并发的损失。

## 8. 微观计数：并发退化的两大来源（cache 与频率），不是 dispatch 延迟

前面的结论只能说明“哪里慢”，这一节用硬件计数回答“为什么”。方法：`perf stat`（本机
`perf_event_paranoid=2`，允许 user-space 计数；`LLC-loads/misses` 在该 AMD PMU 上不支持，
用通用 `cache-misses` + `L1-dcache-*`），10 s 窗口、每档重复 2 次；有效频率 =
`cycles / 进程 CPU 秒`（CPU 秒来自 `/proc/<pid>/task/*/stat`）。

对照组：**hop 模式**（compute→IO→compute 跨调度器）vs **inline 模式**
（`EUGRAPH_IO_INLINE=1`，存储调用在 compute 线程上同步执行），同一 c7 查询、同一 tcmalloc。

### 8.1 c7（小工作集）：hop 的代价主要是 cache 局部性，不是 10 µs 的 dispatch

| 指标（每 op） | hop 1 客户端 | hop 4 客户端 | inline 1 客户端 | inline 4 客户端 |
|---|---:|---:|---:|---:|
| qps | 125.5 / 137.1 | 352.2 / 403.6 | 155.9 / 160.2 | 482.3 / 503.7 |
| instructions | 54.96–58.47 M | 55.04–58.52 M | 54.41–54.81 M | 54.55–54.62 M |
| cycles | 23.82–27.80 M | 25.24–27.81 M | 23.11–23.13 M | 22.53–22.89 M |
| IPC | 2.10–2.31 | 2.10–2.18 | 2.35–2.37 | 2.38–2.42 |
| **cache-miss/op** | **206–272 k** | **232–250 k** | **143–167 k** | **196–199 k** |
| L1 miss rate | 1.51–1.57% | 1.63–1.71% | 1.37–1.49% | 1.56–1.58% |
| 有效 GHz | 3.49–3.69 | 2.73–3.43 | 3.87–4.00 | 3.02–3.24 |

读法：

1. **hop 模式在 1 客户端就有 206–272 k cache-miss/op，inline 只有 143–167 k（高 25–45%）**。
   hop 把数据读进 IO 线程所在核的 cache，再由另一个核上的 compute 线程消费——同一份数据
   跨核走两遍 cache，IPC 因此低 5–12%、cycles/op 高 10–17%。
   **这正是 `executor_hop_bench` 看不到的**：那个 benchmark 的 work 是自旋，没有访存，
   只测了 enqueue/wakeup 的固定成本，所以会得出“并发把 dispatch 成本摊薄”的结论。
2. inline 模式下 cache-miss/op 会随并发上升（143–167 k → 196–199 k，+17~40%）：
   4 条并发查询共享 L3 的真实压力；但 IPC 仍保持 2.38–2.42，说明这些 miss 大部分被
   乱序/并行吸收，所以 qps 仍能到 482–504（相对 hop 高 25–40%）。
3. 指令数只降 1–7%（hop→inline），**说明 dispatch 的指令成本本身不是主因**；
   真正的差别在 stall（IPC）与 cycles/op。

### 8.2 重查询 c9：并发下 cache-miss 与 IPC 明显恶化

| 指标（每 op） | inline 1 客户端 | inline 4 客户端 |
|---|---:|---:|
| qps | 0.6 | 1.8（3.0×） |
| cycles | 6.42 G | 7.21 G（+12%） |
| instructions | 7.99 G | 7.83 G（−2%） |
| IPC | 1.24 | 1.09（−12%） |
| **cache-miss/op** | **36.7 M** | **45.4 M（+24%）** |
| 有效 GHz | 4.16 | 3.36 |

工作集是 c9 每 op 约 1M 行扫描；4 条并发同时扫不同区间，L3 装不下 → miss/op +24%、
IPC −12%、cycles/op +12%。**重查询的并发退化主要是内存层次，不是调度。**

### 8.3 纯计算对照：并发退化可以完全由 DVFS 频率解释

| 指标（每 op） | 1 客户端 | 4 客户端 |
|---|---:|---:|
| qps | 72.3 | 206.3（2.85×） |
| instructions | 137.5 M | 137.5 M（不变） |
| cycles | 52.4 M | 53.2 M（+1.5%） |
| IPC | 2.62 | 2.59 |
| **cache-miss/op** | 130 k | 135 k（+3%，即没有 cache 效应） |
| 有效 GHz | **3.97** | **2.87（−28%）** |

零存储、无 hop 的纯计算查询，4 并发时指令数与 cache-miss 都不变，**唯一变化是频率掉了 28%**
（4 个核心满载 → 这颗 5825U 的功耗/频率墙）。所以“并发数上升、单 op 变慢”里有一大块与代码无关，
任何 4 线程满载的负载都会有。

### 8.4 修正结论

- 并发退化的两个真实来源：**(a) 内存层次**（hop 的跨核数据搬运 + 4 条查询共享 L3；
  重查询最明显，c9 miss/op +24%、IPC −12%）；**(b) DVFS 频率**（4 核满载 −28%，
  纯计算对照证明它与代码无关）。
- **dispatch 延迟不是本质原因**：hop 的开销主要在“数据在两个核之间走两遍 cache”，
  表现为 IPC/cache-miss，而不是那 10 µs 的入队/唤醒；`executor_hop_bench` 因为没有访存，
  看不到这部分。
- 因此 inline 的收益（c7 4 并发 +25~40%）应当理解为“省掉跨核数据搬运 + 减少线程占用”，
  而不是“省掉 21 次 dispatch 的 10 µs”。生产上不默认 inline（会阻塞 compute 线程），
  但这条结论支持**减少存储/计算之间的数据交接**（批量、同线程消费、late materialization）
  这类通用优化。
