# LDBC SNB Interactive SF0.1 对照（最新状态）

> 只保留当前结论：可运行性、最近一次有效耗时，以及剩余性能缺口的原因/思路。
> 历史轮次的过程数据已删除；需要复现时以本文的查询参数和测量口径为准。

## 1. 口径

- 数据：LDBC SNB Interactive **sf0.1**，重新从 Basic CSV 导入；eugraph 327,588 顶点 /
  1,477,965 边，Person 1,528，Message 286,744。
- Person 的 `email` / `speaks` 从独立 CSV 聚合为 `STRING[]` 导入 eugraph；neo4j 同步为 list。
  neo4j 另外存在历史测试标签/边，但标准 Person/Message/Post/Comment/Forum/Tag 等计数与 eugraph 一致。
- neo4j 的 `LIKES.creationDate`、`WORK_AT.workFrom`、Person 日期的字符串属性已归一化为整数；
  eugraph 原本是整数。
- 环境：Release；complex 8 compute / 4 storage IO；update 为一次性数据副本、4 compute、4G cgroup。
- 客户端：官方 Bolt driver，每条查询独立连接；eugraph 的 `id` 是整数，neo4j 是字符串，
  本轮对 neo4j 的 `*Id` 参数统一传字符串，避免“参数类型不匹配 → 0 行 → 虚假加速”。
- 同机交错 A/B；普通查询 4 轮、complex-13/14 2 轮，取 min；complex-14 优化后单独复测 3 轮取 min
  （0.295 s）；绝对值只用于同一轮参考，跨轮次看数量级。
- shortest 类参数：complex-1 `personId=933, firstName=John`；complex-13/14 `person1Id=933, person2Id=1129`
  （官方默认 ID 不在本地 sf0.1，使用真实存在的端点验证，结果已与 neo4j 对照）。
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

> **本分支状态（2026-10-25）**：complex-14 已优化（79.8 s → 0.81 s → **0.295 s**，仍 🔴🔴 约 14×）；
> complex-9 已优化（422 ms → **161.7 ms**，仍 🟠 约 5.0×）；
> complex-5 已优化（514 ms → **264.1 ms**，仍 🟡 约 2.7×）。方案见
> [c5-c9 计划分析](../query/ldbc-c5-c9-plan-analysis.md)、
> [算子实现计划](../query/c5-c9-operator-implementation-plan.md)）。

### 2.1 Short reads

| 查询 | eugraph (ms) | neo4j (ms) | 状态 |
|---|---:|---:|---|
| short-5 | 1.20 | 2.64 | 🟢 快 2.2× |
| short-1 | 1.22 | 2.49 | 🟢 快 2.0× |
| short-4 | 1.23 | 2.49 | 🟢 快 2.0× |
| short-3 | 1.37 | 2.59 | 🟢 快 1.9× |
| short-7 | 1.82 | 3.11 | 🟢 快 1.7× |
| short-6 | 1.80 | 2.86 | 🟢 快 1.6× |
| short-2 | 4.64 | 5.06 | 🟢 快 1.1× |

### 2.2 Complex reads

| 查询 | eugraph (ms) | neo4j (ms) | 状态 |
|---|---:|---:|---|
| complex-13 | 1.54 | 2.50 | 🟢 快 1.6×（shortestPath，length=3） |
| complex-8 | 4.16 | 3.47 | 🟡 慢 1.2× |
| complex-7 | 5.23 | 3.37 | 🟡 慢 1.6× |
| complex-4 | 8.29 | 4.13 | 🟡 慢 2.0× |
| complex-11 | 7.30 | 3.31 | 🟡 慢 2.2× |
| complex-5 | **264.1** | 98.39 | 🟡 慢约 2.7×（已优化，原 513.66） |
| complex-6 | 93.56 | 31.56 | 🟡 慢 3.0× |
| complex-12 | 384.15 | 115.80 | 🟠 慢 3.3× |
| complex-2 | 14.56 | 4.16 | 🟠 慢 3.5× |
| complex-1 | 32.21 | 8.73 | 🟠 慢 3.7×（shortestPath） |
| complex-10 | 40.32 | 10.63 | 🟠 慢 3.8× |
| complex-3 | 132.06 | 28.33 | 🟠 慢 4.7× |
| complex-9 | **161.7** | 32.58 | 🟠 慢约 5.0×（已优化，原 422.39；见 §3） |
| complex-14 | **295** | 20.90 | 🔴🔴 慢约 14×（已优化，原 79,788；见 §3） |

complex-14 为本分支优化后的复测值（3 轮 min，4 行与 neo4j 一致；neo4j 20.9 ms 沿用上一轮同机同数据基线）；
complex-9 为优化后 5 轮 min（20 行与原路径逐值一致；neo4j 32.6 ms 沿用上一轮基线）；
complex-5 为优化后 4 轮 min（20 行与原路径逐值一致；neo4j 98.4 ms 沿用上一轮基线）；其余读取沿用上一轮结果。

### 2.3 Updates

本轮未重跑 update；下表是上一轮同口径结果，**不参与本轮结论**：

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

**当前没有不可运行的 LDBC 读取语句。** complex-14 已跑通并完成优化（79.8 s → 0.295 s，4 行结果与
neo4j 一致，仍慢约 14×）；complex-9 已优化到 161.7 ms（原 422 ms，20 行结果与原路径逐值一致，
仍慢约 5×）；complex-5 已优化到 264.1 ms（原 514 ms，20 行结果与原路径逐值一致，仍慢约 2.7×）；
complex-7/11 的 neo4j 数值属性已归一化为整数后重测，行数分别为 7/7、4/4。

## 3. 剩余性能点、原因与思路

| 优先级 | 查询/范围 | 差距 | 已定位原因 | 下一步思路 |
|---|---|---|---|---|
| P1 | 🟠 complex-9 | 161.7 ms vs 32.6 ms（约 5.0×） | 两层 LM 已消除错位物化（content 168 ms、预过滤 id 105 ms）；剩余为 Filter 的 `creationDate` 在 53,192 行上做 label + per-label vprop 两次点查（约 98 ms），以及 Expand 53k 边（约 45 ms） | 存储/访问路径：compact label、label+hot property 共置、或 `creationDate` 索引；见 [c5-c9 计划分析](../query/ldbc-c5-c9-plan-analysis.md) |
| P1 | 🟡 complex-5 | 264.1 ms vs 98.4 ms（约 2.7×） | 已把 correlated `LeftJoin` 改为每 512 个 left row 执行一次右子计划（CorrelatedSource 多行），消除 8,814 次子计划重启；批大小受 1,024 行 chunk 边界限制，不能直接开大 | 剩余是 89k posts/creator 的 per-edge 成本；可做 OUT/IN 邻接存储或进一步去相关，目标 ≤150 ms。见 [c5-c9 计划分析](../query/ldbc-c5-c9-plan-analysis.md) |
| P1 | 🔴🔴 complex-14 | 295 ms vs 20.9 ms（约 14×） | PC 锚点的 `CrossProduct(CorrelatedSource, LabelScan)` 已改为 `CorrelatedVertexProbe`（缓存 Person 锚点映射，按边端点探测），去掉每个 PC 的 1,528 行标签扫描与过滤；剩余是 24 次 PC 的实际 Comment/Post 展开 | 展开侧批量/邻接缓存，或 PC 去相关；见 [pattern-comprehension-anchor-label-scan.md](../query/pattern-comprehension-anchor-label-scan.md) |
| P1 | 🟠 complex-3 | 4.7× | `Tag(name)` 起点选择、`HAS_CREATOR` 方向的前向/反向表选择仍不理想 | 结合基数估计选起点；按方向选择更合适的边表 |
| P2 | 🟠 complex-1 | 3.7× | shortestPath 后还有 OPTIONAL/collect 属性展开；尚未做最新剖析 | profile 后处理；绝对值只有 ~32 ms，收益有限 |
| P2 | 🟠 complex-10 | 3.8× | VLE 之后的 `IS_LOCATED_IN` / `HAS_CREATOR` 展开顺序 | 调整 join/expand 顺序，减少中间行 |
| P2 | 🟠 complex-12 | 3.3× | `TagClass → Tag → Post` 多跳展开与中间结果规模 | 优化展开顺序/中间结果裁剪 |
| P2 | 🟡 complex-6 | 3.0× | 已通过标签前向扫描从 ~9× 降到 3.0×；剩余是 post/tag 展开与属性读取 | 继续 profile 该路径 |

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
- 同机交错 A/B；普通查询 4 轮、complex-13/14 2 轮，取 min；complex-14 优化后复测 3 轮取 min；
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
