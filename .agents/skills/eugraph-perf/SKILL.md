---
name: eugraph-perf
description: 当用户要求做性能优化、跑 benchmark、做 A/B、分析 PROFILE 或处理性能回归时使用（关键词：优化、性能、perf、benchmark、A/B、PROFILE、on-CPU、off-CPU、gdb、协程栈、长采样、延迟、吞吐、c10、c14）。强制：多方法交叉验证、区分 on-CPU/off-CPU、禁止投机取巧、通用优先、特化/缓存需提案、方向受挫先修不立刻回滚、同二进制交错测量。
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
- 1–4 ms 级差异不写成优化/回归；报告绝对差；
- **采样窗口 ≥ 10 s**（建议 20–30 s）：不要用一次 300 ms 查询的 profile 下结论；短查询用客户端循环持续压，
  让 server 处于稳态；profile 与 A/B 计时分开跑（采样器有开销）；记录窗口、频率、样本数，样本太少就延长；
  至少两个窗口确认稳定。

## 3. 瓶颈分析：多方法交叉 + On/Off-CPU

### 3.1 不要只信一种方法

**PROFILE 本身可能有问题**，性能结论不能只靠它：

- `PROFILE` 会加插桩/改计划/改时序（PROFILE 与 plain run 的计划、耗时可能不同）；
- 算子 `self` 可能把成本藏在子算子/`near` 计数里；`near`/`rows` 是“附近工作量”，不是该算子的纯耗时；
- 采样 profiler 只看到 **on-CPU**；线程阻塞、等 IO、等锁、等调度完全看不见；
- `perf -g` 对 folly 协程栈经常拿不到调用链（DWARF/CFI 与协程帧不匹配），需要 gdb 原生采样兜底（§3.9）；
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
| `perf record`/`report`/`annotate` | on-CPU 热点函数/指令 | 只采样运行中的 CPU；符号/inline 偏差；协程栈常断 |
| gdb 原生采样（`thread apply all bt`） | 协程/优化代码下仍能看到原生线程栈；能看到阻塞叶帧 | 暂停进程；只到原生栈（非逻辑协程链）；需人工聚合；不能用于计时 |
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
| `%CPU` 高，`task-clock/elapsed` 接近实际并行工作的线程数（单线程 ≈ 1），runqueue 低，cswch 低 | **on-CPU**：在算 | `perf record -g` + `perf annotate`；PROFILE self；看 cycles/instructions/cache/branch |
| `%CPU` 低，wall 高，cswch 高，`wchan` 是 `futex`/`io_schedule`/`ep_poll` | **off-CPU blocked**：等锁/IO/网络/定时器 | `offcputime`/`perf sched`/`perf trace`/`strace -c`；WT/IO 统计 |
| `%CPU` 不高但 runqueue > ncpu，nonvoluntary cswch 高 | **off-CPU runnable**：等 CPU/过度订阅 | `perf sched`；线程数/亲和性/协程调度；再决定加不加并发 |
| `%sys` 高、iowait 高 | 内核/IO 路径 | `perf trace`/`strace -c`/`iostat -x`；批量 syscall/IO、减少 fsync |
| CPU 高但几乎不前进 | on-CPU 自旋/锁竞争 | `perf top`/`perf lock`；缩短临界区/退避 |
| 两者都高或交替 | 混合 | 先优化占比大的那一类，改完立刻复测，防止把 off-CPU 转成 on-CPU |

快速命令（工具可能未安装/需 root；不可用时用 `/proc`、`pidstat`、日志计时兜底）：

```bash
# task-clock vs elapsed + on-CPU 效率
# -p 统计进程所有线程；只测某个线程用 -t <tid>
perf stat -p <pid> -e task-clock,cycles,instructions,cache-misses,branch-misses,context-switches,page-faults -- sleep 10

# on-CPU 热点
perf record -F 99 -g -p <pid> -- sleep 10
perf report --stdio --sort comm,dso,symbol
perf annotate <symbol>

# off-CPU / 调度
pidstat -p <pid> 1
pidstat -w -p <pid> 1
cat /proc/<pid>/wchan            # 主线程；多线程看 /proc/<pid>/task/<tid>/wchan
ps -L -p <pid> -o pid,tid,stat,wchan:32,pcpu
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

### 3.9 协程栈：用 gdb 原生采样兜底

folly 协程的栈与 DWARF/CFI 不匹配时，`perf -g` 常常只看到 `resume`/调度器。用 gdb 反复 attach 采样：

```bash
# 1) 持续压目标查询（单连接、出错重连）；不要只跑一次 300ms 查询
python3 /tmp/c3_loop.py 120

# 2) 选对 benchmark server 的 pid，attach 采样 60 次（约 12s，含 attach 开销）
SPID=$(pgrep -x eugraph-server | head -1)
for i in $(seq 1 60); do
  gdb -p "$SPID" -batch -ex "set pagination off" -ex "thread apply all bt 60" -ex detach \
    >> /tmp/c3_samples.txt 2>/dev/null
  sleep 0.2
done
```

要点：

- **`bt 60` 以上**：物理算子在协程帧里很深；`bt 20` 常只看到 `resume`/调度器；
- **排除阻塞叶帧**：`futex`、`pthread_cond_wait`、`epoll`、`__wt_cond_wait` 等是空闲/阻塞线程；
  只统计真正在跑的线程；这些叶帧反过来是 off-CPU 的证据；
- **区分 compute/IO 线程**：compute 线程栈里是算子；IO 线程栈里是
  `IoScheduler::dispatch → SyncGraphDataStore::...`，IO 线程看不到算子的协程调用者，
  要结合执行计划推断“哪个算子发起了这类存储调用”；
- **gdb 会暂停进程**：只用于定位候选热点，不要和 A/B 计时同轮；最终结论仍用同二进制交错 A/B +
  单变量改动验证；
- 采样时长 ≥10 s；样本数太少就延长/增加采样；gdb 不可用（权限/容器）时，可试
  `perf record --call-graph dwarf`，仍不完整则用应用层 trace/计数器，并明确标注局限。
- gdb attach 需要 ptrace 权限；容器/生产环境先确认允许，采样窗口内不要同时跑重负载/其他 profiler。

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

## 10. 本仓库性能复盘（经验清单）

> 来源：complex-3 复盘、c10/c14、VLE、调度器与存储成本等文档。都是已经踩过的坑，优先复用，不要重复试错。
> 详见 [query-performance-optimization-notes](../../../docs/benchmark/query-performance-optimization-notes.md)、
> [storage-operation-costs](../../../docs/benchmark/storage-operation-costs.md)、
> [correlated-endpoint-set-probe](../../../docs/query/engine/correlated-endpoint-set-probe.md)、
> [late-materialization-design](../../../docs/query/engine/late-materialization-design.md)、
> [expand-label-filter-optimization](../../../docs/query/expand-label-filter-optimization.md)、
> [pattern-comprehension-anchor-sharing](../../../docs/query/engine/pattern-comprehension-anchor-sharing.md)、
> [query-value-copy-profiling](../../../docs/benchmark/query-value-copy-profiling.md)、
> [scheduler-hop-throughput](../../../docs/benchmark/scheduler-hop-throughput.md)、
> [ldbc-snb-sf1-comparison](../../../docs/benchmark/ldbc-snb-sf1-comparison.md)。

### 10.1 成本结构

- **结果先对齐，0 行也可能慢**：complex-3 两边都 0 行，eugraph ~490 ms；空结果不代表计划走了捷径。
- **计划相同 ≠ 成本相同**：c3 的 join 顺序/date 谓词位置与 neo4j 基本一致；差距在逐行深拷贝、
  重复 vid 逐元素 seek、空 anon/eprop 查询。
- **重复物化/深拷贝是隐形大头**：52k 行重复构造 VertexValue；批量 API 对重复 vid 仍逐元素查；
  先做 distinct/批量化，但必须量化全局影响（见 10.2 dedup 反例）。
- **IO dispatch 比 seek 贵一个数量级**：WT seek ~0.4–0.6 µs，跨线程 dispatch ~10 µs；
  用 `io_dispatches` 估算，不要只数 seek；c14 批量后 91k→696 dispatch，wall 291→119.5 ms。
- **整对象构造会藏在 `near`/self**：PE ConstructVertex ~65k B-tree reads / ~79 ms；
  看 PE specs/dbHits、`near`，只取需要的列/拓扑。
- **成本模型 > 固定阈值**：标签过滤用 distinct×12 vs label size；按边数/魔数会估错窄标签；
  用标签基数/degree，保留未知时回退。

### 10.2 方案选择

- **去重/缓存/预取不是无条件赚**：LoadVertexPropCoalesce dedup 让 c3/c9 +50–60%，只给 c14 ~6%；已回退。
- **改写 pass 的顺序是语义的一部分**：anchor rewrite 移到 requirements 前会识别失败；
  保留位置 + 后续清理 `PEPlan.coalesce_vertices` 才是可行变体。
- **大改前先估收益上界并实测**：VLE D 方案理论 ~107×，实测 `Expand` 只占 13%（634 ms / 4717 ms），
  上界被大幅下修；先窄实现验证量级再推广。
- **短查询 ROI 低**：4–11 ms 的 c1/c4/c7/short 不优先；先做 c14/c5/c9/c3 等大头。
- **调度 hop 的固定成本**：并发只能填满管线，batch 才摊薄单位 hop 成本；scheduler 微基准见
  [scheduler-hop-throughput](../../../docs/benchmark/scheduler-hop-throughput.md)，不要只加并发。

### 10.3 取证与记录

- **采样只定位，结论靠 A/B**：perf/gdb 只给候选热点；最终用同二进制交错 A/B + 单变量改动验证。
- **可观测性/可测试性**：计数器、计划标记（如 `probe=set`）、可强制预算/回退；
  否则无法证明路径生效、回退分支真的可执行（探针预算测试就是例子）。
- **记录被否证方向**：dedup、prefetch skip、labels fuse、earlier anchor rewrite；没有新证据不要重试。
- **基准口径固定**：同机/同二进制/交错/10 轮 + 3 warmup/取 min；记录 CPU 频率、缓存/线程/txn sync；
  不混 powersave 轮次；结果先对 neo4j。
- **协程栈单独处理**：`perf -g` 不完整时用 gdb（§3.9），bt 60+，排除阻塞叶帧，区分 compute/IO 线程。

## 检查清单

- [ ] 基线/目标/判据已写，机器状态已记录
- [ ] 至少两条独立证据确认瓶颈（没有只依赖 PROFILE 的 self time）
- [ ] 采样窗口 ≥10s（短查询用循环持续压），样本数足够；profile 与 A/B 计时分开
- [ ] 已判定 on-CPU / off-CPU（blocked 还是 runnable），并选了对应的分析/优化手段
- [ ] 协程栈用 perf 不完整时，已用 gdb 原生采样/应用 trace 交叉验证
- [ ] profile 有效性检查做过（观察者效应/计数器语义/符号/采样/冷热）
- [ ] 走通用优先顺序；特化/缓存已提案（如需要）
- [ ] 守卫/回退存在且可测
- [ ] 反向测试/语义对照通过
- [ ] 同二进制交错 A/B，结果行一致；改完重新分类/复测
- [ ] 被否证方向已记录，benchmark 表已更新
