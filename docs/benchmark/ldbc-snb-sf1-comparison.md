# LDBC SNB Interactive SF1 对照（最终版）

> 最新测量：2026-11-10，eugraph `7ec18854`（PC 共享 anchor + 端点 id 提升 + PE 合并读取去重
> + 预取只扫表达式引用的列），Release；neo4j 5.26.32 SF1 实例。
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

| 查询 | eugraph (ms) | neo4j (ms) | eugraph/neo4j |
|---|---:|---:|---:|
| complex-1 | 29.97 | 12.84 | 2.33× |
| complex-2 | 43.09 | 15.98 | 2.70× |
| complex-3 | 1876.8 | 507.3 | 3.70× |
| complex-4 | 58.62 | 16.65 | 3.52× |
| complex-5 | 2636.1 | 1162.4 | 2.27× |
| complex-6 | 46.95 | 418.3 | 0.11×（eugraph 快 8.91×） |
| complex-7 | 6.59 | 3.23 | 2.04× |
| complex-8 | 3.74 | 3.25 | 1.15× |
| complex-9 | 2272.5 | 525.1 | 4.33× |
| complex-10 | 588.60 | 99.46 | 5.92× |
| complex-11 | 40.36 | 15.07 | 2.68× |
| complex-12 | 500.76 | 193.59 | 2.59× |
| complex-13 | 2.05 | 3.06 | 0.67×（eugraph 快 1.50×） |
| complex-14 | **971.38** | 280.75 | 3.46× |
| short-1 | 1.54 | 2.00 | 0.77×（eugraph 快 1.30×） |
| short-2 | 3.64 | 2.72 | 1.34× |
| short-3 | 1.59 | 1.79 | 0.89×（eugraph 快 1.13×） |
| short-4 | 0.99 | 1.59 | 0.62×（eugraph 快 1.61×） |
| short-5 | 1.14 | 1.91 | 0.60×（eugraph 快 1.67×） |
| short-6 | 1.47 | 1.77 | 0.83×（eugraph 快 1.21×） |
| short-7 | 1.50 | 1.71 | 0.87×（eugraph 快 1.14×） |

> c1 的数据差异已消除（见 §1），其耗时计入下方汇总。

**汇总（min 求和）：**

- Complex 14 条：**9,077.6 ms vs 3,257.1 ms = 2.79×**
- Short 7 条：**11.9 ms vs 13.5 ms = 0.88×（eugraph 更快）**
- 全部 21 条：**9,089.4 ms vs 3,270.5 ms = 2.78×**
- 剔除 c1 后 complex：**9,047.6 ms vs 3,244.3 ms = 2.79×**（结论不变）

## 3. 结论

- **c6 是唯一显著快于 neo4j 的查询**：62.4 ms vs 435.2 ms，eugraph 快约 6.97×，
  来自 tag-anchored join reorder。
- **c13 基本持平**（1.02×）；short 合计 1.18×，大部分已在 1.3× 内，short-4/5 更快。
- 主要缺口集中在 **c14（6.43×）**、**c10（5.70×）**、c1（4.10×，数据差异需剔除）、
  c11/c4（3.2–3.4×）、c12/c9/c5/c3（2.4–2.8×）。
- 与优化前 SF1 自比：complex 合计 **12.45 s → 8.58 s（-31%）**；c6 **2.47 s → 62 ms**；
  c5 **3.61 s → 2.79 s（-22.5%）**；c10 **976 ms → 537 ms（-45.5%）**。
