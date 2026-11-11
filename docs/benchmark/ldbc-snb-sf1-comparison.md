# LDBC SNB Interactive SF1 对照（最终版）

> 最新测量：2026-11-11，eugraph 分支 `perf/c10-batch-exact-seek`（`7ac39561`），Release；
> neo4j 5.26.32 SF1 实例。
>
> 本轮优化：c10 的相关端点折叠 / 批量精确 seek / 目的端集合探针 / 目标标签过滤按代价选择 +
> `size([...])` 列表元素拓扑化（见 [correlated-endpoint-set-probe.md](../query/engine/correlated-endpoint-set-probe.md)、
> [late-materialization-design.md](../query/engine/late-materialization-design.md)）；
> 分支同时含 `5d023de8`（labels() 读物化标签列）与 `ffd1ffdd`（Expand 目标标签过滤按代价选择）。
>
> 本轮变化：**complex-10 542.1 → 268.6 ms（-50.4%）**；**complex-14 1098.5 → 775.2 ms（-29.4%）**。
> 其余查询相对 2026-11-10 有 ±5–10% 机器状态波动，未逐条归因。
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
- 测量：2026-11-11；同机、同轮、两条查询文件；**10 轮计时 + 3 轮 warmup**，每轮两引擎各执行一次，
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
| complex-1 | 31.9 | 12.2 | 🟡 慢 2.62× |
| complex-2 | 38.1 | 14.4 | 🟡 慢 2.64× |
| complex-3 | 1318.7 | 550.3 | 🟡 慢 2.40× |
| complex-4 | 49.3 | 15.7 | 🟠 慢 3.14× |
| complex-5 | 2830.3 | 1156.9 | 🟡 慢 2.45× |
| complex-6 | 50.1 | 457.1 | 🟢 快 9.12× |
| complex-7 | 6.4 | 2.1 | 🟠 慢 3.00× |
| complex-8 | 3.8 | 2.5 | 🟡 慢 1.51× |
| complex-9 | 1513.3 | 560.7 | 🟡 慢 2.70× |
| complex-10 | 268.6 | 108.9 | 🟡 慢 2.47× |
| complex-11 | 44.4 | 13.5 | 🟠 慢 3.29× |
| complex-12 | 622.5 | 218.9 | 🟡 慢 2.84× |
| complex-13 | 2.2 | 2.0 | 🟡 慢 1.12× |
| complex-14 | 775.2 | 301.4 | 🟡 慢 2.57× |
| short-1 | 2.1 | 1.9 | 🟡 慢 1.07× |
| short-2 | 4.1 | 2.4 | 🟡 慢 1.73× |
| short-3 | 1.4 | 1.1 | 🟡 慢 1.29× |
| short-4 | 1.2 | 1.3 | 🟢 快 1.13× |
| short-5 | 1.1 | 0.9 | 🟡 慢 1.15× |
| short-6 | 1.5 | 1.1 | 🟡 慢 1.32× |
| short-7 | 1.4 | 1.0 | 🟡 慢 1.33× |

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

- Complex 14 条：**7,555.0 ms vs 3,416.8 ms = 2.21×**
- Short 7 条：**12.7 ms vs 9.8 ms = 1.30×（neo4j 更快）**
- 全部 21 条：**7,567.7 ms vs 3,426.6 ms = 2.21×**
- 剔除 c1 后 complex：**7,523.1 ms vs 3,404.6 ms = 2.21×**（结论不变）

## 3. 结论

- **c6 仍是唯一显著快于 neo4j 的查询**：50.1 ms vs 457.1 ms，eugraph 快 **9.12×**，
  来自 tag-anchored join reorder。
- **c10 本轮优化已生效**：542.1 → **268.6 ms（-50.4%）**，与 neo4j 的差距从 5.58× 收到
  **2.47×**。来源：相关端点折叠 + 批量精确 seek + 目的端集合探针 + 只被 `size()` 消费的
  `collect` 列表元素拓扑化（[correlated-endpoint-set-probe.md](../query/engine/correlated-endpoint-set-probe.md)、
  [late-materialization-design.md](../query/engine/late-materialization-design.md)）。剩余 268.6 ms
  中约 210–240 ms 在 friend 侧：`(friend)<-[:HAS_CREATOR]-(post:Post)` 扫 80,607 条消息 +
  Post 标签过滤约 100 ms，LeftJoin/Aggregate/Distinct 约 110 ms；PC 子树已降到约 63 ms。
- **c14**：1098.5 → **775.2 ms（-29.4%）**，差距 3.89× → **2.57×**；主要来自
  `5d023de8`（labels() 读物化标签列）与 `ffd1ffdd`（Expand 目标标签过滤按代价选择）。
- **short 与 c13**：short 合计 12.7 vs 9.8 ms（1.30×，neo4j 更快），c13 2.2 vs 2.0 ms（1.12×）；
  绝对差都在 1–4 ms，受机器状态/亚毫秒噪声主导，不单独归因。
- 主要缺口（按倍数）：c11 3.29×、c4 3.14×、c7 3.00×、c12 2.84×、c9 2.70×、c2 2.64×、
  c1 2.62×、c14 2.57×、c10 2.47×、c5 2.45×、c3 2.40×。
- 与上一版（2026-11-10，complex 合计 7,735.1 ms）自比：**7,735.1 → 7,555.0 ms（-2.3%）**；
  其中 c10 -273.5 ms、c14 -323.3 ms，其余查询有 ±5–10% 机器状态波动（本机 CPU 频率会漂移，
  见 [known-defects §6](../query/known-defects-todo.md)），因此只对 c10/c14 做归因。
