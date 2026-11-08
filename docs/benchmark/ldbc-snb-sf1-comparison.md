# LDBC SNB Interactive SF1 对照（最终版）

> 最新测量：2026-11-08，eugraph `d4d54f36`（c5 身份需求 + c6 tag reorder + D1 回退 +
> PC 批量化），Release；neo4j 5.26.32 SF1 实例。

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
- c1 结果说明：SF1 neo4j 的 `Person.firstName='Jose'` 为 95 人，eugraph 旧索引路径
  为 147 人。根因已定位为字符串索引等值扫描缺少 value 边界（`Jose` 前缀误匹配
  `Joseph` / `Josef` / `Jose Trinidad`），不是两份数据不一致。索引编码已改为
  `0x04 + escaped bytes + 0x00 0x00` 并增加精确/前缀扫描；重建索引后 c1 的
  行集与 `Jose` 人数（95）已对齐。c1 结果中仍可能出现 `[null,null,null]` 组织
  元组，这是 LDBC driver 的 `asOrganization` 不处理 null tuple，属于独立的
  driver 适配问题。下表 c1 耗时仍来自旧索引版本，仅作参考。

## 2. 结果

| 查询 | eugraph (ms) | neo4j (ms) | eugraph/neo4j |
|---|---:|---:|---:|
| complex-1* | 34.34 | 8.38 | 4.10× |
| complex-2 | 34.84 | 14.67 | 2.37× |
| complex-3 | 1253.86 | 522.32 | 2.40× |
| complex-4 | 50.73 | 15.79 | 3.21× |
| complex-5 | 2793.77 | 1101.71 | 2.54× |
| complex-6 | 62.44 | 435.19 | 0.14×（eugraph 快 6.97×） |
| complex-7 | 5.82 | 2.33 | 2.50× |
| complex-8 | 3.61 | 2.19 | 1.64× |
| complex-9 | 1408.48 | 548.35 | 2.57× |
| complex-10 | 537.48 | 94.31 | 5.70× |
| complex-11 | 42.20 | 12.43 | 3.39× |
| complex-12 | 549.54 | 194.97 | 2.82× |
| complex-13 | 1.69 | 1.66 | 1.02× |
| complex-14 | 1800.93 | 280.04 | 6.43× |
| short-1 | 1.01 | 0.95 | 1.07× |
| short-2 | 2.94 | 1.92 | 1.54× |
| short-3 | 1.25 | 1.27 | 0.99× |
| short-4 | 0.71 | 0.88 | 0.80× |
| short-5 | 0.81 | 0.82 | 0.98× |
| short-6 | 1.15 | 0.90 | 1.27× |
| short-7 | 1.47 | 1.15 | 1.28× |

> `*` complex-1 存在上述 SF1 数据差异，不计入严格语义对比。

**汇总（min 求和）：**

- Complex 14 条：**8,579.7 ms vs 3,234.4 ms = 2.65×**
- Short 7 条：**9.3 ms vs 7.9 ms = 1.18×**
- 全部 21 条：**8,589.1 ms vs 3,242.3 ms = 2.65×**
- 剔除 c1 后 complex：**8,545.4 ms vs 3,226.0 ms = 2.65×**（结论不变）

## 3. 结论

- **c6 是唯一显著快于 neo4j 的查询**：62.4 ms vs 435.2 ms，eugraph 快约 6.97×，
  来自 tag-anchored join reorder。
- **c13 基本持平**（1.02×）；short 合计 1.18×，大部分已在 1.3× 内，short-4/5 更快。
- 主要缺口集中在 **c14（6.43×）**、**c10（5.70×）**、c1（4.10×，数据差异需剔除）、
  c11/c4（3.2–3.4×）、c12/c9/c5/c3（2.4–2.8×）。
- 与优化前 SF1 自比：complex 合计 **12.45 s → 8.58 s（-31%）**；c6 **2.47 s → 62 ms**；
  c5 **3.61 s → 2.79 s（-22.5%）**；c10 **976 ms → 537 ms（-45.5%）**。
