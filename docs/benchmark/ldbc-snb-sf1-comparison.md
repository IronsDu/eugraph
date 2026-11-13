# LDBC SNB Interactive SF1 对照（最终版）

> 最新测量：2026-11-13，eugraph 分支 `perf/single-executor-default`（基于 `af896a14`，
> 含 #313 的默认 tcmalloc 与 #314 的默认同池存储调度），Release；neo4j 5.26.32 SF1 实例。
> 同机、同轮、同两条查询文件；**10 轮计时 + 3 轮 warmup**，每轮两引擎各执行一次，取 min。
>
> 本轮改动（**只改执行模型，不改查询语义/计划**）：
>
> - 存储数据操作默认投递到调用方自己的 compute 池（`same`），不再固定跨到独立 storage IO 池；
>   `BEGIN/COMMIT/ROLLBACK` 仍固定在 IO 池。并发生效：官方 driver complex-only 同二进制 A/B
>   **2 线程 +27%（2.93→3.79 op/s）、4 线程 +66%（3.99→6.60 op/s）**。
> - 进程默认链接 tcmalloc（#313）。
> - 本表是**单客户端、逐条 min** 的延迟口径：任一时刻只有 1 条查询在飞，跨调度器 hop 与
>   并发缓存压力都不参与，因此上表相对上一轮的变化主要是**跨轮机器状态**，不归因于本轮改动。
>   同二进制 A/B（pool vs same）在 7 条 short 上差 ≤0.05 ms（0.58–0.91 vs 0.63–0.87 ms）。
>
> 机器状态：本轮 CPU 频率与负载与上一轮不同（neo4j 的 short min 从 0.86–0.96 ms 降到
> 0.37–0.46 ms、c7 从 3.6 ms 降到 1.1–1.5 ms，eugraph 同向变快），因此**跨轮绝对值不可直接比**；
> 下表的比值是同轮测得的。上一轮（2026-11-12，`perf/ldbc-3x-gap`）complex 合计
> 7,775.74 vs 3,710.63 ms = 2.10×。

## 1. 口径

- 数据：LDBC SNB Interactive **sf1**，eugraph 数据目录 `bench-sf1-latest`，
  neo4j 数据目录 `/home/dodo/code/fuck/neo4j-sf1`。两边完整计数一致：
  **3,181,724 顶点 / 3,055,774 Message / 9,892 Person**。
- 环境：eugraph Release，8 compute / 4 storage IO / 4 GB WT cache /
  `--wt-txn-sync none`，默认存储调度 `same`（数据操作走 compute 池，会话控制留在 IO 池）+
  默认 tcmalloc；neo4j 5.26.32，2 GB heap / 2 GB pagecache，Bolt 8688，
  auth disabled。两边均未为本轮新增二级索引。
- 测量：2026-11-12；同机、同轮、两条查询文件；**10 轮计时 + 3 轮 warmup**，每轮两引擎各执行一次，
  取 min。客户端为官方 Bolt driver。
- 参数：complex/short 用 `personId=933`、`messageId=3`；complex-13/14 用
  `person1Id=933, person2Id=1129`；complex-1 的 `firstName` 保持文件默认 `Jose`。
- c1 结果说明（**差异已消除**）：曾经的差异是 eugraph 旧索引的等值扫描缺少 value
  边界（`Jose` 前缀误匹配 `Joseph` / `Josef` / `Jose Trinidad`），c1 返回 147 行而
  neo4j 返回 95 行。索引编码改为 `0x04 + escaped bytes + 0x00 0x00` 并区分精确/前缀
  扫描后重建索引；2026-11-10 两侧同轮复核：`firstName = 'Jose'` → **95 / 95**，
  `firstName STARTS WITH 'Jose'` → **147 / 147**，c1 返回的 20 行两侧逐行一致
  （`verify_ldbc_results.py --only complex-1`：MATCH）。c1 因此不再有数据差异，
  下表耗时为本轮实测并计入汇总。c1 结果中仍可能出现 `[null,null,null]` 组织元组，
  这是 LDBC driver 的 `asOrganization` 不处理 null tuple，属于独立的 driver 适配问题。

> **小查询（<5 ms）的 JVM 预热敏感性（2026-11-13 追加）**：同一台机器、同一 neo4j 实例，
> c7 的延迟随 JVM 状态在 **1.0 → 3.0 → 8–9 ms** 之间摆动（刚重启后 7–9 ms，连续压测
> ~40 min 后 1.0–1.3 ms，重启后再跑 5 轮 warmup 仍只有 3.0 ms）；short 系列同样从
> 0.4–0.6 ms 摆到 1.5–2.7 ms。大查询（c3/c5/c9/c12/c14）在这些状态下只差 ~5%，因此
> **complex 合计与比值可比，sub-5 ms 的逐条比值只作参考**。表中数据来自已连续服务约
> 40 min 的 neo4j 实例；复现时请保证两边都经过足够长的预热（建议 ≥10 轮全量或 ≥5 min
> 连续负载），并记录 uptime / CPU scaling / swap 状态。

## 2. 结果

| 查询 | eugraph (ms) | neo4j (ms) | 状态 |
|---|---:|---:|---|
| complex-1 | 33.56 | 8.70 | 🟠 慢 3.86× |
| complex-2 | 37.66 | 13.61 | 🟡 慢 2.77× |
| complex-3 | 1297.77 | 518.33 | 🟡 慢 2.50× |
| complex-4 | 52.25 | 14.64 | 🟠 慢 3.57× |
| complex-5 | 2759.09 | 1186.41 | 🟡 慢 2.33× |
| complex-6 | 45.69 | 428.36 | 🟢 快 9.38× |
| complex-7 | 4.91 | 1.27 | 🟠 慢 3.87× |
| complex-8 | 3.12 | 1.61 | 🟡 慢 1.94× |
| complex-9 | 1466.37 | 554.10 | 🟡 慢 2.65× |
| complex-10 | 247.00 | 100.36 | 🟡 慢 2.46× |
| complex-11 | 35.51 | 11.74 | 🟠 慢 3.03× |
| complex-12 | 472.81 | 196.04 | 🟡 慢 2.41× |
| complex-13 | 1.69 | 1.04 | 🟡 慢 1.62× |
| complex-14 | 679.71 | 272.90 | 🟡 慢 2.49× |
| short-1 | 0.97 | 0.63 | 🟡 慢 1.54× |
| short-2 | 2.40 | 1.09 | 🟡 慢 2.20× |
| short-3 | 0.97 | 0.54 | 🟡 慢 1.79× |
| short-4 | 0.57 | 0.41 | 🟡 慢 1.39× |
| short-5 | 0.64 | 0.44 | 🟡 慢 1.47× |
| short-6 | 0.96 | 0.51 | 🟡 慢 1.89× |
| short-7 | 0.95 | 0.52 | 🟡 慢 1.83× |

**速度图例**（以同轮 neo4j 为基准，按 eugraph/neo4j 耗时比值）：

| 图标 | 含义 |
|---|---|
| 🟢 | eugraph 更快（>1×） |
| 🟡 | neo4j 更快，慢 1–3× |
| 🟠 | neo4j 更快，慢 3–5× |
| 🔴 | neo4j 更快，慢 5–10× |
| 🔴🔴 | neo4j 更快，慢 10× 以上 |

> c1 的数据差异已消除（见 §1），其耗时计入下方汇总。

**汇总（min 求和）：**

- Complex 14 条：**7,137.14 ms vs 3,309.11 ms = 2.16×**（逐条比值的 median 为 2.50×）
- Short 7 条：**7.46 ms vs 4.13 ms = 1.80×**（亚毫秒，逐条受噪声主导，见 §1 说明）
- 全部 21 条：**7,144.60 ms vs 3,313.25 ms = 2.16×**
- 剔除 c1 后 complex：**7,103.58 ms vs 3,300.41 ms = 2.15×**（结论不变）
- 合计值按逐条 min 求和，被 c5/c9/c3/c14 主导（合计占 complex 的 86.4%）；
  不要把它当成 14 条的平均水平

## 3. 结论

- **c6 仍是唯一显著快于 neo4j 的查询**：45.7 ms vs 428.4 ms，eugraph 快 **9.38×**，
  来自 tag-anchored join reorder。
- **complex-11**：同二进制交错 A/B 确认本轮标签扫描 memo 生效（−5.8%，重复测 −4.2% ~ −8.0%）；
  PROFILE 的 `totalScanRows` 37,744 → 约 9,000（Company 26,775→1,575、Country 4,551→111），
  同会话 PROFILE wall 35.8 → 29.9 ms。本轮表中 3.03×（上一版 2.82×）同时含机器状态变化，
  不把全部差值归因给本轮改动。
- **complex-4 落到 3.57×**：52.3 ms 中约 **21 ms 是 20,079 次逐顶点标签点读**
  （`post:Post` 过滤，~1.06 µs/次），其余约 11 ms 是 7,524+3,512 次属性点读、
  6 ms 两跳 expand、8 ms 是 Distinct/Aggregate/Project/Sort。计划形状与逐跳行数
  和 neo4j 完全一致（1→5→20,079→2,041→7,204→3,269→0），取数次数还比 neo4j 少
  21%（60,459 vs 73,359 dbHits）；差距来自**单次取数成本**（0.77 µs vs 0.26 µs）
  与“一次语句的存储读串行在单条 IO 线程”的执行模型。
- **complex-7**：4.9 vs 1.3 ms（本轮 neo4j 侧明显变快，绝对差 3.6 ms）；其成本结构为 370 条消息的顶点物化
  （1,110 次点读）+ 28 条 LIKES 展开，属同一“点读成本”问题，不单独归因。
- **short 系列**：7 条合计 7.5 vs 4.1 ms（1.80×）。绝对差只有 0.1~0.4 ms/条，同二进制
  pool/same A/B 的差 ≤0.05 ms；本轮 neo4j 侧 short min 从 0.86–0.96 ms 降到 0.37–0.46 ms，
  比值翻转主要由对端变快造成，不作结论。
- 主要缺口（按比值）：c7 3.87×、c1 3.86×、c4 3.57×、c11 3.03×、c2 2.77×、
  c9 2.65×、c3 2.50×、c14 2.49×、c10 2.46×、c12 2.41×、c5 2.33×、c8 1.94×、c13 1.62×。
  （sub-10 ms 的 c7/c13 与 short 系列受亚毫秒噪声主导，见开头说明。）
- 跨轮：上一轮（2026-11-12）complex 合计 7,775.74 vs 3,710.63 ms = 2.10×；本轮
  **7,137.14 vs 3,309.11 ms = 2.16×**。两边绝对值都下降（neo4j 3,710 → 3,309，eugraph
  7,776 → 7,137），比值基本持平；本轮改动只影响执行模型，单客户端延迟不因此变化，
  不把合计差异记到本轮改动上。

## 4. 下一层的瓶颈（未在本轮处理）

- **单次点读成本**：eugraph 0.77 µs/次 vs neo4j 0.26 µs/次（c4 实测）。我们每次读是
  “拼 key + WT B-tree `search_near` + 解码”，neo4j 是定长记录按偏移读 page cache，
  标签/属性常在同一条记录页里（它的 `post:Post` 检查与 `tag.name` 读取不额外计费）。
- **单语句的存储读串行**：同一语句任一时刻只有一个存储调用在飞（默认 `same` 下它在
  compute 线程上执行，不再钉在单条 IO 线程），其余读只能等前一个返回
  （c4 的标签 PE：`io=1`、20,079 次读、wall 21 ms 而 cpu ~4 ms）。并行化需要多 session，
  属于事务/会话模型改动。
- 可选但都属 **T2**（KV 编码 / 并发模型）的方向：把目的端标签并入边索引条目、把多属性
  点读合并成一次记录读、按批把点读并行到多个 IO 线程。未提案前不动。
- complex-11 的剩余时间在 2,572 行 `friend` 的属性点读（9.3 ms，实测换成 3 个扁平属性
  同样价）与两跳逐源点查上；再往下需要 late-materialization 的 LIMIT 切分（已知的
  ownership 待办），不在本轮范围。
