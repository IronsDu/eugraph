# LDBC SNB Interactive SF1 对照（最终版）

> 最新测量：2026-11-12，eugraph 分支 `perf/ldbc-3x-gap`（基于 `71b12755`），Release；
> neo4j 5.26.32 SF1 实例。同机、同轮、同两条查询文件；**10 轮计时 + 3 轮 warmup**，
> 每轮两引擎各执行一次，取 min。
>
> 本轮改动（只对能归因的项下结论）：
>
> - `id()` / `elementId()` 不再触发 whole-vertex 物化（identity-only 消费，见
>   [late-materialization-design.md](../query/engine/late-materialization-design.md)）。
>   独立微基准：`MATCH (n:Person) RETURN count(id(n))` **37.70 → 1.61 ms（−95.7%）**，
>   计划里不再有 `ProjectionExtract`。SF1 的 21 条读查询都不依赖这一形态，故下表不受影响。
> - Expand 目的端标签扫描按算子 memo，不再每 chunk 重扫整张标签（见
>   [expand-label-filter-optimization.md](../query/expand-label-filter-optimization.md) §6）。
>   同二进制交错 A/B：**complex-11 −5.8%**（重复测 −4.2% ~ −8.0%），其余查询 ±1%。
> - 被否证并回退的两个方向（记录在此，避免重试）：批量标签点读按 vid 升序（c4 无改善）、
>   `getVertexCoalesceDataBatch` 按 vid 去重（c4 **+6~9% 回归**）。
>
> 机器状态：本轮 CPU scaling 62–72%（上一版约 72–100%）。neo4j 侧同轮也变慢
> （c5 1156.9 → 1324.3 ms），因此**跨轮绝对值不可直接比**；下表的比值是同轮测得的。
>
> 历史（2026-11-11，`perf/c10-batch-exact-seek`）：complex-10 542.1 → 268.6 ms、
> complex-14 1098.5 → 775.2 ms；同轮 complex 合计 7,555.0 vs 3,416.8 ms（2.21×）。
>
> 历史（2026-11-10）：`LoadVertexPropCoalesce` 的“按 distinct 顶点去重 + 查询级缓存”在 c14 上只省
> ~6%，却让 complex-3 1256→1865 ms、complex-9 1430→2272 ms（+49%/+59%），因此回退；
> c14 当时为 1,098.5 ms（仍比 1800.9 ms 快 39%）。

## 1. 口径

- 数据：LDBC SNB Interactive **sf1**，eugraph 数据目录 `bench-sf1-latest`，
  neo4j 数据目录 `/home/dodo/code/fuck/neo4j-sf1`。两边完整计数一致：
  **3,181,724 顶点 / 3,055,774 Message / 9,892 Person**。
- 环境：eugraph Release，8 compute / 4 storage IO / 4 GB WT cache /
  `--wt-txn-sync none`；neo4j 5.26.32，2 GB heap / 2 GB pagecache，Bolt 8688，
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

## 2. 结果

| 查询 | eugraph (ms) | neo4j (ms) | 状态 |
|---|---:|---:|---|
| complex-1 | 32.2 | 11.6 | 🟡 慢 2.77× |
| complex-2 | 38.1 | 16.0 | 🟡 慢 2.38× |
| complex-3 | 1345.8 | 581.6 | 🟡 慢 2.31× |
| complex-4 | 52.0 | 17.2 | 🟠 慢 3.02× |
| complex-5 | 3015.7 | 1324.3 | 🟡 慢 2.28× |
| complex-6 | 52.7 | 482.4 | 🟢 快 9.16× |
| complex-7 | 7.4 | 3.6 | 🟡 慢 2.06× |
| complex-8 | 4.7 | 3.3 | 🟡 慢 1.42× |
| complex-9 | 1557.2 | 607.7 | 🟡 慢 2.56× |
| complex-10 | 266.7 | 130.8 | 🟡 慢 2.04× |
| complex-11 | 39.9 | 14.1 | 🟡 慢 2.82× |
| complex-12 | 558.6 | 209.0 | 🟡 慢 2.67× |
| complex-13 | 2.3 | 3.0 | 🟢 快 1.31× |
| complex-14 | 802.6 | 306.1 | 🟡 慢 2.62× |
| short-1 | 1.5 | 2.1 | 🟢 快 1.40× |
| short-2 | 4.5 | 3.6 | 🟡 慢 1.26× |
| short-3 | 2.2 | 2.6 | 🟢 快 1.20× |
| short-4 | 1.3 | 2.0 | 🟢 快 1.54× |
| short-5 | 1.3 | 2.2 | 🟢 快 1.65× |
| short-6 | 1.9 | 2.1 | 🟡 慢 1.12× |
| short-7 | 2.0 | 2.0 | 🟡 慢 1.04× |

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

- Complex 14 条：**7,775.9 ms vs 3,710.7 ms = 2.10×**
- Short 7 条：**14.7 ms vs 16.6 ms = eugraph 更快（1.13×）**
- 全部 21 条：**7,790.6 ms vs 3,727.3 ms = 2.09×**
- 剔除 c1 后 complex：**7,743.7 ms vs 3,699.1 ms = 2.09×**（结论不变）

## 3. 结论

- **c6 仍是唯一显著快于 neo4j 的查询**：52.7 ms vs 482.4 ms，eugraph 快 **9.16×**，
  来自 tag-anchored join reorder。
- **complex-11**：同二进制交错 A/B 确认本轮标签扫描 memo 生效（−5.8%，重复测 −4.2% ~ −8.0%）；
  PROFILE 的 `totalScanRows` 37,744 → 约 9,000（Company 26,775→1,575、Country 4,551→111），
  同会话 PROFILE wall 35.8 → 29.9 ms。表中 2.82×（上一版 3.29×）同时含机器状态变化，
  不把全部差值归因给本轮改动。
- **complex-4 落到 3.02×**：52.0 ms 中约 **21 ms 是 20,079 次逐顶点标签点读**
  （`post:Post` 过滤，~1.06 µs/次），其余约 11 ms 是 7,524+3,512 次属性点读、
  6 ms 两跳 expand、8 ms 是 Distinct/Aggregate/Project/Sort。计划形状与逐跳行数
  和 neo4j 完全一致（1→5→20,079→2,041→7,204→3,269→0），取数次数还比 neo4j 少
  21%（60,459 vs 73,359 dbHits）；差距来自**单次取数成本**（0.77 µs vs 0.26 µs）
  与“一次语句的存储读串行在单条 IO 线程”的执行模型。
- **complex-7**：7.4 vs 3.6 ms，绝对差 3.8 ms；其成本结构为 370 条消息的顶点物化
  （1,110 次点读）+ 28 条 LIKES 展开，属同一“点读成本”问题，不单独归因。
- **short 系列**：7 条合计 14.7 vs 16.6 ms，eugraph 反超；绝对差 0.1~1 ms，
  受亚毫秒噪声主导，不作结论。
- 主要缺口（按比值）：c4 3.02×、c11 2.82×、c1 2.77×、c12 2.67×、c14 2.62×、
  c9 2.56×、c2 2.38×、c3 2.31×、c5 2.28×、c7 2.06×、c10 2.04×。
- 与上一版（2026-11-11，complex 合计 7,555.0 vs 3,416.8 ms = 2.21×）自比：
  本轮 complex 合计 **7,775.9 vs 3,710.7 ms = 2.10×**。绝对耗时两边都上升
  （neo4j c5 1156.9 → 1324.3 ms，−14% 机器频率），因此只对同二进制 A/B 的
  c11 做归因，不把合计变化记到本轮改动上。

## 4. 下一层的瓶颈（未在本轮处理）

- **单次点读成本**：eugraph 0.77 µs/次 vs neo4j 0.26 µs/次（c4 实测）。我们每次读是
  “拼 key + WT B-tree `search_near` + 解码”，neo4j 是定长记录按偏移读 page cache，
  标签/属性常在同一条记录页里（它的 `post:Post` 检查与 `tag.name` 读取不额外计费）。
- **单 IO 线程串行**：一次语句的存储读都发给同一条 storage-IO 线程，compute 线程等待
  （c4 的标签 PE：`io=1`、20,079 次读、wall 21 ms 而 cpu ~4 ms）。并行化需要多 session，
  属于事务/会话模型改动。
- 可选但都属 **T2**（KV 编码 / 并发模型）的方向：把目的端标签并入边索引条目、把多属性
  点读合并成一次记录读、按批把点读并行到多个 IO 线程。未提案前不动。
- complex-11 的剩余时间在 2,572 行 `friend` 的属性点读（9.3 ms，实测换成 3 个扁平属性
  同样价）与两跳逐源点查上；再往下需要 late-materialization 的 LIMIT 切分（已知的
  ownership 待办），不在本轮范围。
