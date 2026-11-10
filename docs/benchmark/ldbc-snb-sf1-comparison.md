# LDBC SNB Interactive SF1 对照（最终版）

> 最新测量：2026-11-10，eugraph `6bc12410`（PC 共享 anchor + 端点 id 提升 + 预取只扫表达式
> 引用的列；**PE 合并读取去重已回退**，见下），Release；neo4j 5.26.32 SF1 实例。
>
> 回退说明：`LoadVertexPropCoalesce` 的"按 distinct 顶点去重 + 查询级缓存"在 c14 上只省
> ~6%，却让 **complex-3 1256→1865 ms、complex-9 1430→2272 ms**（+49%/+59%，同数据集
> 同机 A/B 定位到该提交），因此回退；c14 现为 1,101.7 ms（仍比 1800.9 ms 快 39%）。
> 本轮变化最大的是 **complex-14：1800.9 ms → 971.4 ms**（共享 anchor + 三处按行 IO 修复，
> 详见 [query/engine/pattern-comprehension-anchor-sharing.md](../query/engine/pattern-comprehension-anchor-sharing.md)）。

## 1. 口径

- 数据：LDBC SNB Interactive **sf1**，eugraph 数据目录 `eugraph-data-sf1`，
  neo4j 数据目录 `/home/dodo/code/fuck/neo4j-sf1`。两边完整计数一致：
  **3,181,724 顶点 / 3,055,774 Message / 9,892 Person**。
- 环境：eugraph Release，8 compute / 4 storage IO / 4 GB WT cache /
  `--wt-txn-sync none`；neo4j 5.26.32，2 GB heap / 2 GB pagecache，Bolt 8688，
  auth disabled。两边均未为本轮新增二级索引。
- 测量：同机、同轮、两条查询文件；**10 轮计时 + 3 轮 warmup**，每轮两引擎各执行一次，
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
| complex-1 | 31.25 | 12.83 | 🟡 慢 2.44× |
| complex-2 | 34.83 | 15.76 | 🟡 慢 2.21× |
| complex-3 | 1231.7 | 498.1 | 🟡 慢 2.47× |
| complex-4 | 47.55 | 16.01 | 🟡 慢 2.97× |
| complex-5 | 2705.9 | 1157.8 | 🟡 慢 2.34× |
| complex-6 | 46.48 | 420.5 | 🟢 快 9.05× |
| complex-7 | 6.36 | 3.19 | 🟡 慢 1.99× |
| complex-8 | 3.61 | 3.21 | 🟡 慢 1.13× |
| complex-9 | 1421.4 | 522.6 | 🟡 慢 2.72× |
| complex-10 | 542.1 | 97.21 | 🔴 慢 5.58× |
| complex-11 | 40.04 | 13.49 | 🟡 慢 2.97× |
| complex-12 | 523.5 | 197.0 | 🟡 慢 2.66× |
| complex-13 | 2.02 | 3.19 | 🟢 快 1.58× |
| complex-14 | 1098.5 | 282.5 | 🟠 慢 3.89× |
| short-1 | 1.19 | 1.73 | 🟢 快 1.45× |
| short-2 | 3.57 | 3.04 | 🟡 慢 1.17× |
| short-3 | 1.39 | 1.74 | 🟢 快 1.25× |
| short-4 | 1.02 | 1.53 | 🟢 快 1.51× |
| short-5 | 1.22 | 1.79 | 🟢 快 1.47× |
| short-6 | 1.53 | 1.75 | 🟢 快 1.15× |
| short-7 | 1.52 | 1.46 | 🟡 慢 1.04× |

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

- Complex 14 条：**7,735.1 ms vs 3,243.3 ms = 2.38×**
- Short 7 条：**11.4 ms vs 13.1 ms = 0.88×（eugraph 更快）**
- 全部 21 条：**7,746.5 ms vs 3,256.4 ms = 2.38×**
- 剔除 c1 后 complex：**7,703.9 ms vs 3,230.5 ms = 2.38×**（结论不变）

## 3. 结论

- **c6 是唯一显著快于 neo4j 的查询**：46.5 ms vs 420.5 ms，eugraph 快约 9.05×，
  来自 tag-anchored join reorder。
- **c13 反而更快**（1.58×，eugraph 快）；short 合计 **0.88×**（eugraph 更快），
  其中 short-1/3/4/5/6 全部反超，short-2/7 在 1.0–1.2×。
- 主要缺口集中在 **c10（5.58×）**、**c14（3.89×）**、c4/c11（2.97×）、
  c9/c12/c3（2.5–2.7×）、c1/c5（2.3–2.4×）。
- 与上一版本（2026-11-08）自比：complex 合计 **8.58 s → 7.74 s（-9.8%）**；
  **c14 1800.9 → 1098.5 ms（-39%）**；c12 549.5 → 523.5 ms；c3 1253.9 → 1231.7 ms；
  c9 1408.5 → 1421.4 ms（噪声内）。
