---
name: eugraph-perf
description: 当用户要求做性能优化、跑 benchmark、做 A/B、分析 PROFILE 或处理性能回归时使用（关键词：优化、性能、perf、benchmark、A/B、PROFILE、on-CPU、off-CPU、延迟、吞吐、c10、c14）。强制：多方法交叉验证、区分 on-CPU/off-CPU、禁止投机取巧、通用优先、特化/缓存需提案、方向受挫先修不立刻回滚、同二进制交错测量。
---

# EuGraph 性能优化协议

## 何时使用

- 用户要求“优化 X”“把 cN 降到 X ms”“看下这个算子为什么慢”；
- 需要跑 LDBC benchmark、做 A/B、分析 PROFILE、处理性能回归。

## 0. 三条红线

1. **不投机取巧**：不按查询文本/ID/label 特判、不缓存结果、不跳过语义工作、不把工作挪出计时区间、不改测试/基准；
2. **通用优先**：算法/计划 → 访问路径/IO → 执行效率 → 缓存（提案）→ 特化（提案）；
3. **方向 ≠ 实现**：方向正确但实验失败，先定位是副作用/常量/缺失路径，至少 2 次针对性修复后再决定；不静默回滚整个方向。

特化/缓存不是绝对禁止，但**必须先用短提案拿到开发者确认**。

## 1. 立项（改代码前写清）

- **基线**：当前数值、测量命令、机器状态；
- **目标**：可度量（如 c10 ≤ 300 ms）与判据；
- **假设**：热点与机制（先有 profile/证据，不靠猜）；
- **非目标**：本次不动什么；
- **风险**：语义、并发、内存、兼容面。

## 2. 基线测量

- 性能结论必须**同二进制交错 A/B**：同一台机器、同一数据、两条查询交替跑；
- 先确认机器状态（本机 CPU 频率会漂移，可差 ~2×；`lscpu` 的 scaling MHz / 当前频率）；
- 文档/收尾基准：**10 轮 + 3 warmup，取 min/p50**；探索期可用 3–5 轮微 A/B；
- 记录数据目录、线程数、cache 大小、neo4j 版本；
- 1–4 ms 级差异不写成优化/回归；报告绝对差。

## 3. 瓶颈分析：多方法交叉 + On/Off-CPU

### 3.1 不要只信一种方法

**PROFILE 本身可能有问题**，性能结论不能只靠它：

- `PROFILE` 会加插桩/改计划/改时序（PROFILE 与 plain run 的计划、耗时可能不同）；
- 算子 `self` 可能把成本藏在子算子/`near` 计数里；`near`/`rows` 是“附近工作量”，不是该算子的纯耗时；
- 采样 profiler 只看到 **on-CPU**；线程阻塞、等 IO、等锁、等调度完全看不见；
- 符号/inline/优化后的代码可能归因到错误函数；短查询采样点太少；
- 计数器语义可能被误读（例如把“输入行数”当成“distinct 数”），一次读数不能当结论；
- 单次运行、warm cache、CPU 频率漂移都会让数字不可比。

**最低要求**：至少两条**独立**证据（如：端到端 A/B + 算子计数器；perf 采样 + 定向计时；
off-CPU 分析 + 差分实验）。只有一条时，把它标成假设，用 A/B 或差分实验验证后再下结论。
两条证据冲突时，不要挑一个相信：设计一个能区分两者的实验（禁用组件/换参数/加计时点）。

### 3.2 方法矩阵

| 方法 | 能回答 | 盲区 |
|---|---|---|
| 端到端 wall time + 结果行 | 是否真的变快、语义是否一致 | 不知道慢在哪 |
| `PROFILE` 算子树 self/inclusive + `chunks`/`ioDispatches`/`near`/`scanRows` | 哪个算子/哪类工作量占主导；batching 是否生效 | 插桩影响、`near` 语义、阻塞不可见 |
| `perf stat`（cycles/instructions/cache/branch/task-clock） | on-CPU 效率、是否 CPU-bound | 不告诉具体热点函数；off-CPU 不可见 |
| `perf record`/`report`/`annotate` | on-CPU 热点函数/指令 | 只采样运行中的 CPU；符号/inline 偏差 |
| off-CPU 分析（`perf sched`、`offcputime`、`pidstat -w`、`wchan`） | 阻塞在 IO/锁/调度/网络的哪里、多久 | 需要权限/工具；对短事件分辨率有限 |
| 资源指标（`vmstat`、`mpstat`、`iostat`、`ss`） | 系统是否 CPU/IO/内存/网络饱和 | 不告诉代码位置 |
| 定向计时/计数器/`EUGRAPH_OP_TRACE`/`EUGRAPH_IO_TRACE` | 某个具体阶段的真实耗时/次数 | 需要改代码或开 trace；有开销 |
| 差分实验（禁用组件/换参数/换路径） | 某个机制是否真的贡献了时间 | 可能改变计划/语义，需单变量 |
| 微基准（隔离函数/算子） | 单点实现效率 | 与真实查询的访存/并发不同 |

> 本仓库的算子/IO trace 开关与字段见 [指标与诊断日志](../../../docs/debugging/metrics-and-tracing.md)。

> 经验：先用 wall time 判断“有没有问题”，再用 on/off-CPU 分类，最后才用代码级工具定位。

### 3.3 先分类：On-CPU 还是 Off-CPU

| 观测 | 倾向 | 下一步 |
|---|---|---|
| `%CPU` 高，`task-clock/elapsed ≈ ncpu`，runqueue 低，cswch 低 | **on-CPU**：在算 | `perf record -g` + `perf annotate`；PROFILE self；看 cycles/instructions/cache/branch |
| `%CPU` 低，wall 高，cswch 高，`wchan` 是 `futex`/`io_schedule`/`ep_poll` | **off-CPU blocked**：等锁/IO/网络/定时器 | `offcputime`/`perf sched`/`perf trace`/`strace -c`；WT/IO 统计 |
| `%CPU` 不高但 runqueue > ncpu，nonvoluntary cswch 高 | **off-CPU runnable**：等 CPU/过度订阅 | `perf sched`；线程数/亲和性/协程调度；再决定加不加并发 |
| `%sys` 高、iowait 高 | 内核/IO 路径 | `perf trace`/`strace -c`/`iostat -x`；批量 syscall/IO、减少 fsync |
| CPU 高但几乎不前进 | on-CPU 自旋/锁竞争 | `perf top`/`perf lock`；缩短临界区/退避 |
| 两者都高或交替 | 混合 | 先优化占比大的那一类，改完立刻复测，防止把 off-CPU 转成 on-CPU |

快速命令（工具可能未安装/需 root；不可用时用 `/proc`、`pidstat`、日志计时兜底）：

```bash
# task-clock vs elapsed + on-CPU 效率
perf stat -p <pid> -e task-clock,cycles,instructions,cache-misses,branch-misses,context-switches,page-faults -- sleep 10

# on-CPU 热点
perf record -F 99 -g -p <pid> -- sleep 10
perf report --stdio --sort comm,dso,symbol
perf annotate <symbol>

# off-CPU / 调度
pidstat -p <pid> 1
pidstat -w -p <pid> 1
cat /proc/<pid>/wchan
grep -E 'voluntary|nonvoluntary' /proc/<pid>/status
perf sched record -- sleep 5 && perf sched latency
offcputime -p <pid> 10        # bcc，需 root/内核支持；不可用则用 perf sched + pidstat

# 系统层
vmstat 1 5
mpstat -P ALL 1 5
iostat -x 1 5
```

> 采样式工具（`perf record`、`perf top`、`offcputime`）本身有开销。**不要**在正在采集 A/B 数字的同一轮里开重采样；
> 先单独采集 profile，再用干净的同一二进制做 A/B。

### 3.4 On-CPU 问题的分析与优化

**分析**：

- `perf stat` 看 IPC、cache-miss、branch-miss：高 cycles/instruction、cache/branch 失败 → 数据布局/分支/访存问题；
- `perf record/report/annotate` 找具体函数/指令；结合 PROFILE 的算子 self/inclusive；
- 高 `%sys` 时看 syscall/page fault/copy（`perf trace`、`strace -c`、`/proc/<pid>/stat`）；
- 自旋/锁竞争用 `perf lock`/`perf top` 确认，不要把所有高 CPU 都当成“算得多”。

**优化方向**：

- 减少工作量：算法/计划（去相关、join 顺序、裁剪、避免中间结果/重复扫描）；
- 访问路径：批量 seek、精确索引、扫描剪枝、减少物化；
- 执行效率：向量化、typed path、减少拷贝/分配、改善数据布局、热路径去虚调用/分支；
- 内核侧：批量 syscall、减少 page fault/小 IO、避免不必要的 fsync；
- **不要**用“加线程”解决单线程 on-CPU 饱和；先证明有并行空间且无锁竞争。

### 3.5 Off-CPU 问题的分析与优化

先区分 **blocked**（等 IO/锁/网络/定时器）和 **runnable**（等 CPU）：

- **Blocked**：`wchan` 指向 `futex`/`io_schedule`/`ep_poll` 等；cswch 高；`offcputime` 按栈聚合等待时间；
- **Runnable**：runqueue 高、nonvoluntary cswch 高；`perf sched latency`/`timehist` 看等 CPU 的时间；
- **IO**：`iostat -x`、WT 统计、`EUGRAPH_IO_TRACE`；区分读/写、随机/顺序、fsync；
- **锁**：`perf lock`/futex 栈；确认临界区长度与竞争线程数；
- **网络**：`ss`、`perf trace`；确认是等待还是吞吐瓶颈。

**优化方向**：

- IO：批量/合并、预取、异步/重叠、较大读、减少 fsync/写放大；WT cache/txn 配置（改配置前先确认影响）；
- 锁：缩短临界区、分片/降低共享、减少锁 convoy；不要用“加锁粒度更细”掩盖错误共享；
- 调度：线程池大小、避免过度订阅、亲和性、协程/任务调度；先看 runqueue 再决定加并发；
- 等待：事件驱动替代 sleep/轮询；backpressure/批处理网络消息；
- **注意**：异步/缓存/加线程可能把 off-CPU 成本转成 on-CPU 或内存成本；改完必须重新分类、复测。

### 3.6 Profile 有效性检查（下结论前）

- PROFILE 与 plain run 的计划/耗时是否一致？插桩是否改变了 batching/并行？
- 计数器的语义是什么（输入行？输出行？distinct？near 包含哪些工作）？能否用第二个计数器交叉验证？
- 采样是否有符号/inline/优化导致的错误归因？采样频率和运行时长够不够？
- 是否是 warm cache/单次运行/CPU 频率漂移造成的假象？能否同二进制交错复测？
- 是否只看到了 on-CPU，而真正的瓶颈在 off-CPU？
- 若仍不确定：加一个**定向计时点**或做**差分实验**（禁用可疑组件），让数据说话。

### 3.7 复测

- 找到真正的热点后再改；**每次优化后重新 profile/分类**，热点会转移；
- 不拿旧 profile 继续优化；不在旧热点上重复投入；
- 改动并发/异步/缓存后，重新判断 on-CPU/off-CPU，不要假设原来的瓶颈还在。

### 3.8 本仓库的落地例子

- **先看执行形状**：c10 优化时 `chunks` 从 ~200 变成 ~21,000，说明 batching 被关、退化成逐行；
  这是 on-CPU 工作量放大，先修执行路径，不是加线程。
- **`near` 会藏成本**：PE 的整对象构造可能不在 `self` 里；用 `ioDispatches`/`wtSearchNear`/`PE specs`
  加差分实验（禁用构造/只取 ref）交叉验证，别只看 self time。
- **两个计数器交叉**：标签过滤用 `labelFetchCount` 与 `labelScanCount` 确认实际走了哪条路径；
  两个都为 0 说明优化没生效，而不是“优化没收益”。
- **off-CPU 信号**：wall time 高但 `%CPU` 低、`wchan` 是 `futex`/`io_schedule` 时，先看 WT/IO 统计、
  `perf sched`/`offcputime`，不要继续在算子 self time 里找；批量/预取/异步改完后再复测 on-CPU。

## 4. 实现

- 先选通用机制，并在方案里说明“为什么上一层不够”；
- 新路径必须有**守卫/回退**（数据分布、degree、预算超限、只读门控等）；
- 同时加**可观测性与可测试性**：计数器、计划标记（如 `probe=set`）、可强制预算/回退；
  否则无法证明优化生效、回退分支可执行；
- 需要特化/缓存时，先按 §7 提案，不要在未确认时落地。

## 5. 正确性验证（与性能同等重要）

- 先写能区分对错的判据；bugfix 先证明 fixture 在缺陷存在时会失败；
- 定向测试 + 受影响 TCK 子集 + 必要时 LDBC 21 条对 neo4j；
- 反向测试：守卫必须“该退避时退避”（如 `head(list)`、第二个 UNWIND、用户写 `id()` 等值、自环/平行边）；
- 语义结果行必须与基线一致（包括重数/顺序）。

## 6. A/B 与报告

- 同二进制、交错执行；报告 min/p50、机器状态、数据/参数；
- 结果行一致；性能提升与语义正确性分开陈述；
- 文档/收尾时更新 benchmark 表（`docs/benchmark/`），注明轮数、warmup、日期、机器状态；
- 只对能归因的查询做归因；其余标注噪声/机器漂移。

## 7. 特化 / 缓存提案模板

在 Issue 评论或 PR 描述中写：

- 特化/缓存了什么，适用面多大；
- 为什么通用方案覆盖不了（附证据）；
- 失效条件、回退路径、一致性/失效策略、内存上限、并发影响；
- 预期收益、维护成本、风险；
- 验证方式（冷/热两种口径）。

**拿到确认后再写代码。**

## 8. 方向受挫协议

1. 单变量隔离：确认是方向本身、还是副作用/常量/缺失的批处理路径；
2. 分类：正确性 / 性能 / 复杂度 / 环境（机器频率、数据污染）；
3. 至少 2 次针对性修复尝试（有时间盒），记录证据；
4. 仍不行：带“证据 + 已尝试修法 + 选项（继续/换方案/回滚）”找开发者；
5. 只回退副作用，保留方向主干；**不静默回滚**。

## 9. 收尾

- 记录被否证的方向（文档/Issue），避免重复；
- 更新 benchmark 表/设计文档；
- 按 `eugraph-verify` 的 L2（PR 前）验证；L3 全量 TCK 优先交给 CI；
- PR 描述包含基线/目标/证据/未验证项。

## 检查清单

- [ ] 基线/目标/判据已写，机器状态已记录
- [ ] 至少两条独立证据确认瓶颈（没有只依赖 PROFILE 的 self time）
- [ ] 已判定 on-CPU / off-CPU（blocked 还是 runnable），并选了对应的分析/优化手段
- [ ] profile 有效性检查做过（观察者效应/计数器语义/符号/采样/冷热）
- [ ] 走通用优先顺序；特化/缓存已提案（如需要）
- [ ] 守卫/回退存在且可测
- [ ] 反向测试/语义对照通过
- [ ] 同二进制交错 A/B，结果行一致；改完重新分类/复测
- [ ] 被否证方向已记录，benchmark 表已更新
