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
- 同机交错 A/B；普通查询 4 轮、complex-13/14 2 轮，取 min；绝对值只用于同一轮参考，跨轮次看数量级。
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
| complex-14 | **79788** | **20.90** | 🔴 慢 ~3800×（PC 全表扫描，见 §3） |

### 2.3 Updates

本轮只重测读取；update 未重跑，下表是上一轮同口径结果，**不参与本轮结论**：

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

**当前没有不可运行的 LDBC 读取语句。** complex-14 已跑通，4 行结果与 neo4j 一致；
complex-7/11 的 neo4j 数值属性已归一化为整数后重测，行数分别为 7/7、4/4。

## 3. 剩余性能点、原因与思路

| 优先级 | 查询/范围 | 差距 | 已定位原因 | 下一步思路 |
|---|---|---|---|---|
| P0 | 🔴 complex-14 | 79.8 s vs 20.9 ms | PC 子计划为 `AllNodeScan(a) → Expand → Expand → Expand → Filter`；`a.id = startNode(r).id` 只在末尾过滤，没变成索引 seek；每个 `r` 都从全部 Person 扫起 | 把 PC 的端点等式（OR 两方向）下推为索引 seek，或拆成两个 PC 分支做 union；消除 `AllNodeScan(a)` |
| P1 | 🔴 complex-9 | 13.0× | 长链 VLE/展开和中间结果规模是主要嫌疑；本轮未做最新逐算子剖析 | 补 plan + profiling，确认扫描/展开/属性读取的主成本；再决定 VLE 剪枝/索引 |
| P1 | 🔴 complex-5 | 5.2× | 低 degree 的 `post→HAS_CREATOR` 访问放大：大量 source 各读 1 条边，全局 `edge_index` 的 per-source 定位成本占主导 | 增加按 (label, direction) 的邻接索引/边表局部性，避免每个 source 重新定位 B+Tree |
| P1 | 🟡 complex-3 | 4.7× | `Tag(name)` 起点选择、`HAS_CREATOR` 方向的前向/反向表选择仍不理想 | 结合基数估计选起点；按方向选择更合适的边表 |
| P2 | 🟡 complex-1 | 3.7× | shortestPath 后还有 OPTIONAL/collect 属性展开；尚未做最新剖析 | profile 后处理；绝对值只有 ~32 ms，收益有限 |
| P2 | 🟡 complex-10 | 3.8× | VLE 之后的 `IS_LOCATED_IN` / `HAS_CREATOR` 展开顺序 | 调整 join/expand 顺序，减少中间行 |
| P2 | 🟡 complex-12 | 3.3× | `TagClass → Tag → Post` 多跳展开与中间结果规模 | 优化展开顺序/中间结果裁剪 |
| P2 | 🟡 complex-6 | 3.0× | 已通过标签前向扫描从 ~9× 降到 3.0×；剩余是 post/tag 展开与属性读取 | 继续 profile 该路径 |

### 3.1 通用方向

1. **边邻接局部性**：当前低 degree 展开的主要成本是全局 `edge_index` 的 per-source 定位，
   可考虑按 `(edge_label, direction, src/dst)` 建邻接表，或至少让同 source 的连续展开复用 cursor。
2. **相关子计划下推**：complex-14 暴露出 pattern comprehension 的端点等式没有参与索引选择；
   这是“表达式级相关子查询”的通用优化点，不只影响 LDBC。
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
- 同机交错 A/B；普通查询 4 轮、complex-13/14 2 轮，取 min。
- 全量 ctest：**1304/1304**（含 TCK）。
