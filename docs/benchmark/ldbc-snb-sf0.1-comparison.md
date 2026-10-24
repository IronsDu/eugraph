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
  （0.81 s，首测 1.17 s）；绝对值只用于同一轮参考，跨轮次看数量级。
- shortest 类参数：complex-1 `personId=933, firstName=John`；complex-13/14 `person1Id=933, person2Id=1129`
  （官方默认 ID 不在本地 sf0.1，使用真实存在的端点验证，结果已与 neo4j 对照）。
- **点查索引前提**：short-4..7 依赖 `Person/Message/Post/Comment/Forum(id)` 索引。
  本轮 eugraph 补建 `Message.id` 索引，neo4j 的对应索引均确认 `ONLINE`；无索引时 short-4 会退化为
  全表扫描，不能作为结论。

**速度图例**（以同查询 neo4j 为基准）：

| 图标 | 含义 |
|---|---|
| 🟢 | 不超过 2×（含比 neo4j 快） |
| 🟡 | 慢 2–5× |
| 🔴 | 慢 5× 以上 |

## 2. 可运行性与耗时

> **本分支状态（2026-10-24）**：complex-14 已完成优化（79.8 s → 0.81 s，仍 🔴 约 39×）；
> complex-5 / complex-9 **尚未优化**，根因与算子方案已确定（见
> [c5-c9 计划分析](../query/ldbc-c5-c9-plan-analysis.md)、
> [算子实现计划](../query/c5-c9-operator-implementation-plan.md)），待 PR 合并后继续。

### 2.1 Short reads

| 查询 | eugraph (ms) | neo4j (ms) | 状态 |
|---|---:|---:|---|
| short-1 | 1.22 | 2.49 | 🟢 快 2.0× |
| short-2 | 4.64 | 5.06 | 🟢 快 1.1× |
| short-3 | 1.37 | 2.59 | 🟢 快 1.9× |
| short-4 | 1.23 | 2.49 | 🟢 快 2.0× |
| short-5 | 1.20 | 2.64 | 🟢 快 2.2× |
| short-6 | 1.80 | 2.86 | 🟢 快 1.6× |
| short-7 | 1.82 | 3.11 | 🟢 快 1.7× |

### 2.2 Complex reads

| 查询 | eugraph (ms) | neo4j (ms) | 状态 |
|---|---:|---:|---|
| complex-1 | 32.21 | 8.73 | 🟡 慢 3.7×（shortestPath） |
| complex-2 | 14.56 | 4.16 | 🟡 慢 3.5× |
| complex-3 | 132.06 | 28.33 | 🟡 慢 4.7× |
| complex-4 | 8.29 | 4.13 | 🟡 慢 2.0× |
| complex-5 | 513.66 | 98.39 | 🔴 慢 5.2× |
| complex-6 | 93.56 | 31.56 | 🟡 慢 3.0× |
| complex-7 | 5.23 | 3.37 | 🟢 慢 1.6× |
| complex-8 | 4.16 | 3.47 | 🟢 慢 1.2× |
| complex-9 | 422.39 | 32.58 | 🔴 慢 13.0× |
| complex-10 | 40.32 | 10.63 | 🟡 慢 3.8× |
| complex-11 | 7.30 | 3.31 | 🟡 慢 2.2× |
| complex-12 | 384.15 | 115.80 | 🟡 慢 3.3× |
| complex-13 | 1.54 | 2.50 | 🟢 快 1.6×（shortestPath，length=3） |
| complex-14 | **810** | 20.90 | 🔴 慢约 39×（已优化，原 79,788；见 §3） |

complex-14 为本分支优化后的复测值（3 轮 min，4 行与 neo4j 一致；neo4j 20.9 ms 沿用上一轮同机同数据基线）；
其余读取沿用上一轮同口径结果，complex-5/9 本轮未优化，数值同上轮。

### 2.3 Updates

本轮未重跑 update；下表是上一轮同口径结果，**不参与本轮结论**：

| 语句 | eugraph (ms) | neo4j (ms) | 状态 |
|---|---:|---:|---|
| update-1 | 53.7 | 23.9 | 🟡 慢 2.2× |
| update-2 | 7.55 | 51.95 | 🟢 快 6.9× |
| update-3 | 7.75 | 57.62 | 🟢 快 7.4× |
| update-4 | 26.08 | 17.01 | 🟢 慢 1.5× |
| update-5 | 7.64 | 15.15 | 🟢 快 2.0× |
| update-6 | 26.62 | 20.64 | 🟢 慢 1.3× |
| update-7 | 440.07 | 96.08 | 🟡 慢 4.6× |
| update-8 | 7.45 | 13.34 | 🟢 快 1.8× |

**当前没有不可运行的 LDBC 读取语句。** complex-14 已跑通并完成优化（79.8 s → 0.81 s，4 行结果与
neo4j 一致，仍慢约 39×）；complex-7/11 的 neo4j 数值属性已归一化为整数后重测，行数分别为 7/7、4/4。
complex-5/9 尚未优化，留待 PR 合并后继续（见 §3）。

## 3. 剩余性能点、原因与思路

| 优先级 | 查询/范围 | 差距 | 已定位原因 | 下一步思路 |
|---|---|---|---|---|
| P0 | 🔴 complex-9 | 422 ms vs 32.6 ms（13.0×） | `coalesce(content, imageFile)` 在 7,706 行上全部物化（约 168 ms）；且 RETURN 的 `Project` 位于 `Limit` 之下，TopN 之后无法再延迟物化 | 拆分 RETURN Project + `LateMaterialize`：Sort/Limit 只带键，Limit 之上再物化 content/姓名；目标 ≤250 ms。方案见 [c5-c9 计划分析](../query/ldbc-c5-c9-plan-analysis.md) |
| P0 | 🔴 complex-5 | 514 ms vs 98.4 ms（5.2×） | `friend IN friends` 在 `CONTAINER_OF` 89k posts 与 `HAS_CREATOR` 89k 边之后才过滤，89k 中间行全部物化后丢弃；per-source Expand 又放大了这个顺序 | `BatchExpand`（>2048 行改一次扫边 + hash join）或 vertex-list semi-join/join reorder；保留 OPTIONAL 零计数；目标 ≤300 ms。方案见 [算子实现计划](../query/c5-c9-operator-implementation-plan.md) |
| P1 | 🔴 complex-14 | 810 ms vs 20.9 ms（约 39×） | 原 `AllNodeScan(a)` 已改为 `LabelScan(Person)`，端点等式已下推到第一次 Expand 之前；剩余是每个 `r` 仍各评估一次 PC，端点 `id` 未走索引 seek（未做最新逐算子剖析） | 运行时索引 seek（`a.id = startNode(r).id`）或 PC 去相关；属结构性改动，暂列后续 |
| P1 | 🟡 complex-3 | 4.7× | `Tag(name)` 起点选择、`HAS_CREATOR` 方向的前向/反向表选择仍不理想 | 结合基数估计选起点；按方向选择更合适的边表 |
| P2 | 🟡 complex-1 | 3.7× | shortestPath 后还有 OPTIONAL/collect 属性展开；尚未做最新剖析 | profile 后处理；绝对值只有 ~32 ms，收益有限 |
| P2 | 🟡 complex-10 | 3.8× | VLE 之后的 `IS_LOCATED_IN` / `HAS_CREATOR` 展开顺序 | 调整 join/expand 顺序，减少中间行 |
| P2 | 🟡 complex-12 | 3.3× | `TagClass → Tag → Post` 多跳展开与中间结果规模 | 优化展开顺序/中间结果裁剪 |
| P2 | 🟡 complex-6 | 3.0× | 已通过标签前向扫描从 ~9× 降到 3.0×；剩余是 post/tag 展开与属性读取 | 继续 profile 该路径 |

优先级按“差距 + 下一步可执行性”综合：c5/c9 已有可实施的算子方案、且是 PR 合并后的首项，列 P0；
c14 剩余优化属结构性改动，列 P1。

### 3.1 通用方向

1. **边邻接局部性**：当前低 degree 展开的主要成本是全局 `edge_index` 的 per-source 定位，
   可考虑按 `(edge_label, direction, src/dst)` 建邻接表，或至少让同 source 的连续展开复用 cursor。
2. **相关子计划下推**：complex-14 已完成第一步——PC anchor 从 `AllNodeScan` 改为 `LabelScan`，
   并把端点等式下推到第一次 Expand 之前（79.8 s → 0.81 s）；剩余是让端点 `id` 走运行时索引
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
- 同机交错 A/B；普通查询 4 轮、complex-13/14 2 轮，取 min；complex-14 优化后复测 3 轮取 min。
- complex-14 优化后：0.81 s（首测 1.17 s），4 行，与 neo4j 一致；计划由 `AllNodeScan(a)` 变为
  `LabelScan(Person)` + 端点谓词 Filter（见
  [pattern-comprehension-anchor-label-scan.md](../query/pattern-comprehension-anchor-label-scan.md)）。
- complex-5/9 未在本分支改动，数值沿用上一轮；算子方案已写入
  [c5-c9-operator-implementation-plan.md](../query/c5-c9-operator-implementation-plan.md)。
- 全量 ctest：**1304/1304**（含 TCK）。
