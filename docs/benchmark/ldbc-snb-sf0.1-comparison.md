# LDBC SNB Interactive SF0.1 对照（最新状态）

> 只保留当前结论：可运行性、最近一次有效耗时，以及剩余性能缺口的原因/思路。
> 历史轮次的过程数据已删除；需要复现时以本文的查询参数和测量口径为准。

## 1. 口径

- 数据：LDBC SNB Interactive **sf0.1**，重新从 Basic CSV 导入；eugraph 327,588 顶点 /
  1,477,965 边，Person 1,528，Message 286,744。
- **结果复验使用 CsvComposite 数据**（`social_network-sf0.1-CsvComposite-LongDateFormatter`，
  schema 为 `docs/program/design/ldbc-sf01-composite.schema.json`）：`email` 作为 `STRING[]` 导入；
  Person 的 `language` 列按上游 `cypher/scripts/headers.txt` 映射为 `speaks:STRING[]`
  （`language` 是 Post 的属性，不是 Person 的）。
  neo4j 另外存在历史测试标签/边，但标准 Person/Message/Post/Comment/Forum/Tag 等计数与 eugraph 一致。
- neo4j 的 `LIKES.creationDate`、`WORK_AT.workFrom`、Person 日期的字符串属性已归一化为整数；
  eugraph 原本是整数。
- 环境：Release；complex 8 compute / 4 storage IO；update 为一次性数据副本、4 compute、4G cgroup。
- 客户端：官方 Bolt driver，每条查询独立连接；eugraph 的 `id` 是整数，neo4j 是字符串，
  本轮对 neo4j 的 `*Id` 参数统一传字符串，避免“参数类型不匹配 → 0 行 → 虚假加速”。
- 同机交错 A/B；全量 21 条读查询 1 轮 warmup + 3 轮计时，取 min；
  c5/c9/c14 另有 5 轮专项复测。绝对值受机器频率影响，跨轮次只看数量级。
- 参数：complex/short 的 `personId=933`、`messageId=3`；complex-13/14
  `person1Id=933, person2Id=1129`（官方默认 ID 不在本地 sf0.1）。complex-1 的 `firstName`
  保持文件默认 `Jose`。
- **点查索引前提**：short-4..7 依赖 `Person/Message/Post/Comment/Forum(id)` 索引。
  本轮 eugraph 补建 `Message.id` 索引，neo4j 的对应索引均确认 `ONLINE`；无索引时 short-4 会退化为
  全表扫描，不能作为结论。

**速度图例**（以同查询 neo4j 为基准，按 eugraph/neo4j 耗时比值）：

| 图标 | 含义 |
|---|---|
| 🟢 | eugraph 更快（<1×） |
| 🟡 | neo4j 更快，慢 1–3× |
| 🟠 | neo4j 更快，慢 3–5× |
| 🔴 | neo4j 更快，慢 5–10×（红） |
| 🔴🔴 | neo4j 更快，慢 10× 以上 |

各性能表按 eugraph/neo4j 比值升序排列：eugraph 相对最快（或差距最小）的在前，最慢的在后。

## 2. 可运行性与耗时

> **本分支状态（2026-10-25，全量 21 条复跑）**：complex-14 **351 ms**（🔴🔴 约 21×）；
> complex-9 **232 ms**（🔴 约 8.1×，旧 161.7 为错误计划结果）；complex-5 **250 ms**（🟡 约 2.8×）。
> 三者行结果均与 neo4j 一致；c1/c12 的差异见 §2.2 注。方案见
> [c5-c9 计划分析](../query/ldbc-c5-c9-plan-analysis.md)、
> [算子实现计划](../query/c5-c9-operator-implementation-plan.md)）。

### 2.1 Short reads

| 查询 | eugraph (ms) | neo4j (ms) | 状态 |
|---|---:|---:|---|
| short-5 | 1.16 | 1.81 | 🟢 快 1.6× |
| short-6 | 1.44 | 2.06 | 🟢 快 1.4× |
| short-4 | 0.95 | 1.34 | 🟢 快 1.4× |
| short-1 | 1.76 | 2.13 | 🟢 快 1.2× |
| short-3 | 1.51 | 1.80 | 🟢 快 1.2× |
| short-7 | 1.42 | 1.45 | 🟡 约 1.0×（0 行） |
| short-2 | 5.21 | 2.88 | 🟡 慢 1.8× |

### 2.2 Complex reads

| 查询 | eugraph (ms) | neo4j (ms) | 状态 |
|---|---:|---:|---|
| complex-13 | 1.49 | 2.07 | 🟢 快 1.4×（shortestPath） |
| complex-8 | 4.58 | 3.24 | 🟡 慢 1.4× |
| complex-7 | 6.26 | 3.10 | 🟡 慢 2.0× |
| complex-11 | 7.63 | 3.74 | 🟡 慢 2.0× |
| complex-4 | 8.49 | 3.74 | 🟡 慢 2.3×（0 行） |
| complex-2 | 9.96 | 3.94 | 🟡 慢 2.5× |
| complex-6 | 77.32 | 29.08 | 🟡 慢 2.7× |
| complex-5 | **249.76** | 89.53 | 🟡 慢 2.8× |
| complex-1 | 18.26 | 5.22 | 🟠 慢 3.5×（15/15；列表顺序归一后一致） |
| complex-12 | 380.55 | 99.05 | 🟠 慢 3.8×（仅 tag 列表顺序不同） |
| complex-3 | 109.85 | 27.66 | 🟠 慢 4.0×（0 行） |
| complex-10 | 44.29 | 9.24 | 🟠 慢 4.8× |
| complex-9 | **231.90** | 28.71 | 🔴 慢 8.1× |
| complex-14 | **351.14** | 16.46 | 🔴🔴 慢 21.3× |

> 全量 21 条读查询复跑（3 轮 min，同轮交错；Message.id 索引已建）。
> **CsvComposite 复验：21/21 条语义一致**。c12 的 `collect(DISTINCT tag.name)` 和
> c1 的 email/speaks/unis/companies 列表顺序由 Cypher 保证；按集合归一后逐行一致。
> c1 的重复行来自“列表含 null 的 GROUP BY 分组键”缺陷，已由 `valueContentEquals` 修复
> （首段 shortestPath 15/15，最终 c1 也是 15/15）。
> c3/c4 当前参数下两边都是 0 行，耗时只代表扫描/过滤成本。

complex-5/9/14 的上表为全量 21 条同轮 A/B（3 轮 min）；计划文档中的 234/187/291 ms
是另外 5 轮专项复测，绝对值受机器频率影响，结论以同轮比值为准。

**complex-9 更正**：旧的 161.7 ms 来自 filter-boundary 延迟物化缺陷——
Sort 所需、Filter 不需要的 `message.id` 被放在 Sort 下方按 LIMIT 后物化，
Sort 比较了占位值，返回的 Top-20 不是最大日期。该优化已移除；修复后为 187.2 ms，
优化前正确基线是 422 ms。见 [vertex-data-design §正确性修复](../storage/vertex-data-design.md)。
其余读取沿用上一轮结果。

### 2.3 Updates

本轮未获得有效的 update 对比：8 条 `interactive-update-*` 是写语句，参数来自
LDBC update stream fixture（文件本身没有 `:param` 块），仓库内没有现成 fixture；
用空参数执行是 no-op，不能作为结果。下表沿用上一轮同口径数据，**不参与本轮结论**：

| 语句 | eugraph (ms) | neo4j (ms) | 状态 |
|---|---:|---:|---|
| update-3 | 7.75 | 57.62 | 🟢 快 7.4× |
| update-2 | 7.55 | 51.95 | 🟢 快 6.9× |
| update-5 | 7.64 | 15.15 | 🟢 快 2.0× |
| update-8 | 7.45 | 13.34 | 🟢 快 1.8× |
| update-6 | 26.62 | 20.64 | 🟡 慢 1.3× |
| update-4 | 26.08 | 17.01 | 🟡 慢 1.5× |
| update-1 | 53.7 | 23.9 | 🟡 慢 2.2× |
| update-7 | 440.07 | 96.08 | 🟠 慢 4.6× |

**当前没有不可运行的 LDBC 读取语句。** 全量 21 条结果均语义一致：
complex-14 351 ms（4 行）、complex-9 232 ms（旧 161.7 为错误计划结果）、
complex-5 250 ms；complex-1/c12 仅在 `collect` 列表顺序上不同，按集合归一后 15/15、3/3。
complex-3/4 当前参数下两边均为 0 行。

## 3. 剩余性能点、原因与思路

| 优先级 | 查询/范围 | 差距 | 已定位原因 | 下一步思路 |
|---|---|---|---|---|
| P1 | 🔴 complex-9 | 232 ms vs 28.7 ms（约 8.1×） | 融合 `labels + coalesce` 已把 53k 行的 label/property 读取并成一次前缀扫描；Filter 边界 LM 因正确性问题已移除，Sort key 恢复正常物化。剩余是 Expand 53k 边的 per-edge 成本 + Sort/PE | 展开侧批量/共享 cursor；见 [c5-c9 计划分析](../query/ldbc-c5-c9-plan-analysis.md) |
| P1 | 🟡 complex-5 | 250 ms vs 89.5 ms（约 2.8×） | correlated `LeftJoin` 已按 512 行批处理；`scanEdgesBatch` 已改为每 4,096 个 source 复用 cursor，消除 98k 次 per-source cursor | 剩余是 89k posts/creator 的 per-edge 成本与去相关；目标 ≤150 ms。见 [c5-c9 计划分析](../query/ldbc-c5-c9-plan-analysis.md) |
| P1 | 🔴🔴 complex-14 | 351 ms vs 16.5 ms（约 21×） | PC 锚点的 `CrossProduct(CorrelatedSource, LabelScan)` 已改为 `CorrelatedVertexProbe`（缓存 Person 锚点映射，按边端点探测），去掉每个 PC 的 1,528 行标签扫描与过滤；剩余是 24 次 PC 的实际 Comment/Post 展开 | 展开侧批量/邻接缓存，或 PC 去相关；见 [pattern-comprehension-anchor-label-scan.md](../query/pattern-comprehension-anchor-label-scan.md) |
| P1 | 🟠 complex-3 | 4.0×（0 行） | `Tag(name)` 起点选择、`HAS_CREATOR` 方向的前向/反向表选择仍不理想 | 结合基数估计选起点；按方向选择更合适的边表 |
| P2 | 🟠 complex-1 | 3.5×（20/15 行，数据差异） | shortestPath 后还有 OPTIONAL/collect 属性展开；尚未做最新剖析 | profile 后处理；绝对值只有 ~32 ms，收益有限 |
| P2 | 🟠 complex-10 | 4.8× | VLE 之后的 `IS_LOCATED_IN` / `HAS_CREATOR` 展开顺序 | 调整 join/expand 顺序，减少中间行 |
| P2 | 🟠 complex-12 | 3.8×（仅列表顺序差异） | `TagClass → Tag → Post` 多跳展开与中间结果规模 | 优化展开顺序/中间结果裁剪 |
| P2 | 🟡 complex-6 | 2.7× | 已通过标签前向扫描从 ~9× 降到 3.0×；剩余是 post/tag 展开与属性读取 | 继续 profile 该路径 |

优先级按“差距 + 下一步可执行性”综合：c5/c9/c14 的剩余优化都落到存储/访问路径或更大结构调整，
均列 P1（c5 已完成算子层批处理）。

### 3.1 通用方向

1. **边邻接局部性**：当前低 degree 展开的主要成本是全局 `edge_index` 的 per-source 定位，
   可考虑按 `(edge_label, direction, src/dst)` 建邻接表，或至少让同 source 的连续展开复用 cursor。
2. **相关子计划下推**：complex-14 已完成第一步——PC anchor 从 `AllNodeScan` 改为 `LabelScan`，
   并把端点等式下推到第一次 Expand 之前（79.8 s → 0.295 s）；剩余是让端点 `id` 走运行时索引
   seek、或对 PC 去相关。这个“表达式级相关子查询”方向对非 LDBC 查询同样适用。
3. **属性批量/惰性加载**：path 顶点现在按需 load labels/properties；complex-14 已因此正确，
   但大量路径元素时仍应批量预取，避免逐元素往返。
4. **内存**：shortestPath/allShortestPaths 的 BFS 路径枚举有 100k 条上限，超过抛明确错误；
   sf0.1 未触发 OOM，4G cgroup 下无失控。

## 4. 本轮验证

- 数据重新导入（Basic CSV + 适配 schema），eugraph：327,588 顶点 / 1,477,965 边；
  Person 1,528 / Message 286,744。
- Person 的 `email`/`speaks` 从 `person_email_emailaddress` / `person_speaks_language`
  聚合为 `STRING[]` 后导入；neo4j 同步写入同名 list 属性。
- neo4j 的 `LIKES.creationDate` / `WORK_AT.workFrom` / Person 日期属性归一化为整数；
  eugraph 原本就是整数。
- eugraph 创建 `Message.id` 索引，neo4j 的 Person/Message/Post/Comment/Forum id 索引均 ONLINE。
- 同机交错 A/B；全量 21 条读查询 warmup 1 + 3 轮取 min；c5/c9/c14 另有 5 轮专项复测；
  complex-9 优化后复测 5 轮取 min。
- complex-14 优化后：0.295 s（3 轮 min），4 行，与 neo4j 一致；PC 锚点由
  `CrossProduct(CorrelatedSource, LabelScan(Person))` 改为 `CorrelatedVertexProbe`，
  去掉 24 次重复 LabelScan（见
  [pattern-comprehension-anchor-label-scan.md](../query/pattern-comprehension-anchor-label-scan.md)）。
- complex-9 优化后：161.7 ms（原路径 415.4 ms），20 行与原路径逐值一致；计划为
  `Project → LateMaterialize(20 行) → Limit → Sort → LateMaterialize(7,706 行) → Filter → PE`。
- complex-5 优化后：264.1 ms（原路径 510.4 ms），20 行与原路径逐值一致；相关 LeftJoin 改为
  每 512 行批量执行右子计划（见 [c5-c9 计划分析](../query/ldbc-c5-c9-plan-analysis.md)）。
- 全量 ctest：**1304/1304**（含 TCK）。
