# LDBC SNB Interactive SF0.1 对照（最新状态）

> 只保留当前结论：可运行性、最近一次有效耗时，以及剩余性能缺口的原因/思路。
> 历史轮次的过程数据已删除；需要复现时以本文的查询参数和测量口径为准。

## 1. 口径

- 数据：LDBC SNB Interactive **sf0.1**，eugraph 官方加载口径。
- 环境：Release；complex 8 compute / 4 storage IO；update 为一次性数据副本、4 compute、4G cgroup。
- 客户端：官方 Bolt driver，每条查询独立连接；eugraph 的 `id` 是整数，neo4j 是字符串。
- 同机交错 A/B；本机 CPU 频率会漂移，绝对值只用于同一轮参考，跨轮次看数量级。
- shortest 类参数：complex-1 `personId=933, firstName=John`；complex-13/14 `person1Id=933, person2Id=1129`
  （官方默认 ID 不在本地 sf0.1，使用真实存在的端点验证，结果已与 neo4j 对照）。
- complex-1 选 `John` 是因为两边都返回 20 行；`Jose` 在本地数据上 eugraph/neo4j 行数为 20/15，
  数据不一致，不适合做耗时对照。
- **点查索引前提**：short-4..7 依赖 neo4j 的 `Person/Message/Post/Comment/Forum(id)` 索引。
  本轮已补建并确认 `ONLINE`；未建索引时 short-4 会退化为全表扫描（~78 ms），
  会得到虚假的 70–98× 加速，不能作为结论。

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
| short-1 | 1.19 / 1.53 | 1.99 / 2.25 | 🟢 快 1.7× |
| short-2 | 4.21 / 5.11 | 3.57 / 3.97 | 🟢 慢 1.2× |
| short-3 | 1.34 / 1.41 | 1.99 / 2.42 | 🟢 快 1.5× |
| short-4 | 0.91–0.96 / 1.0–1.2 | 1.89–2.34 / 2.22–2.71 | 🟢 快 2.1–2.4× |
| short-5 | 0.99–1.06 / 1.21 | 2.05–2.11 / 2.14–2.25 | 🟢 快 2.0× |
| short-6 | 1.29 / 1.69 | 2.41 / 2.79 | 🟢 快 1.9× |
| short-7 | 2.25 / 2.64 | 2.85 / 3.44 | 🟢 快 1.3× |

### 2.2 Complex reads

| 查询 | eugraph (ms) | neo4j (ms) | 状态 |
|---|---:|---:|---|
| complex-1 | 29.2 / 29.4 | 7.99 / 8.41 | 🟡 慢 3.7×（shortestPath） |
| complex-2 | 13.73 / 15.41 | 4.67 / 4.85 | 🟡 慢 2.9× |
| complex-3 | 131.5 / 136.7 | 26.63 / 27.10 | 🟡 慢 4.9× |
| complex-4 | 7.59 / 8.28 | 3.95 / 4.58 | 🟢 慢 1.9× |
| complex-5 | 475.6 / 488.4 | 90.07 / 92.70 | 🔴 慢 5.3× |
| complex-6 | 550–605 | 61–87 | 🔴 慢 ~9×（最近有效测量） |
| complex-7 | 4.82 / 5.53 | 4.27 / 4.72 | 🟢 慢 1.1× |
| complex-8 | 3.53 / 3.78 | 3.74 / 4.02 | 🟢 快 1.06× |
| complex-9 | 3960 | 37 | 🔴 慢 ~107×（最近有效测量） |
| complex-10 | 30.44 / 31.62 | 9.26 / 9.87 | 🟡 慢 3.3× |
| complex-11 | 7.02 / 7.79 | 4.16 / 4.73 | 🟢 慢 1.7× |
| complex-12 | 337.0 / 348.5 | 97.98 / 104.01 | 🟡 慢 3.4× |
| complex-13 | 5.06 / 5.22 | 5.13 / 5.70 | 🟢 基本持平（shortestPath，length=3） |
| complex-14 | **86000** | **2900** | 🔴 可运行，慢 ~30×（PC 全表扫描，见 §3） |

### 2.3 Updates

eugraph 在一次性数据副本上真实提交；neo4j 在现有库上真实提交后按创建/关系条件清理，清理已核验无残留。
2 轮取 min（首轮含冷态），4 compute、4G cgroup；是单条 update 延迟，不是 QPS 基准。

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

**当前没有不可运行的 LDBC 语句。** complex-5 不再挂死；complex-14 已跑通且结果与 neo4j 完全一致。

## 3. 剩余性能点、原因与思路

| 优先级 | 查询/范围 | 差距 | 已定位原因 | 下一步思路 |
|---|---|---|---|---|
| P0 | 🔴 complex-14 | 86.0 s vs 2.9 s（~30×） | PC 子计划为 `AllNodeScan(a) → Expand → Expand → Expand → Filter`；`a.id = startNode(r).id` 只在末尾过滤，没变成索引 seek；每个 `r` 都从全部 Person 扫起 | 把 PC 的端点等式（OR 两方向）下推为索引 seek，或拆成两个 PC 分支做 union；消除 `AllNodeScan(a)` |
| P1 | 🔴 complex-9 | ~107× | 尚未做最新剖析；长链 VLE/展开和中间结果规模是主要嫌疑 | 先补 plan + profiling，确认是扫描、展开还是属性读取；再决定 VLE 剪枝/索引 |
| P1 | 🔴 complex-6 | ~9× | 同上；当前只有最近有效测量，没有最新热点 | 先 profile，再按热点处理 |
| P1 | 🔴 complex-5 | 5.3× | 低 degree 的 `post→HAS_CREATOR` 访问放大：大量 source 各读 1 条边，全局 `edge_index` 的 per-source 定位成本占主导 | 增加按 (label, direction) 的邻接索引/边表局部性，避免每个 source 重新定位 B+Tree |
| P1 | 🟡 complex-3 | 4.9× | `Tag(name)` 起点选择、`HAS_CREATOR` 方向的前向/反向表选择仍不理想 | 结合基数估计选起点；按方向选择更合适的边表 |
| P2 | 🟡 complex-1 | 3.7× | shortestPath 后还有 OPTIONAL/collect 属性展开；尚未做最新剖析 | profile 后处理；绝对值只有 ~29 ms，收益有限 |
| P2 | 🟡 complex-12 | 3.4× | `TagClass → Tag → Post` 多跳展开与中间结果规模 | 优化展开顺序/中间结果裁剪 |
| P2 | 🟡 complex-10 | 3.3× | VLE 之后的 `IS_LOCATED_IN` / `HAS_CREATOR` 展开顺序 | 调整 join/expand 顺序，减少中间行 |
| P2 | 🟡 update-1/7 | 2.2× / 4.6× | 写路径：update-1 创建 Person + 多条边，update-7 创建 Comment:Message + 多条边/属性；尚未拆热点 | 先按 label/属性/边创建拆分 profile |

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

- complex-14：4 行，86.0 s；`personIdsInPath` / `pathWeight` 与 neo4j 逐行一致。
- 新增 TCK feature：`tests/tck/features-eugraph/shortest-path/shortest-path.feature`（3/3 通过）。
- 新增单测：`PatternComprehensionAsReduceListSource`；原有 `ReduceBodyCorrelatedPatternComprehension` 通过。
- 全量 ctest：**1304/1304**（含 TCK）。

---

## 5. 2026-10-23：vertex_data / 邻接缓存实验

> 本节是存储格式改造后的独立复测，**§2 的旧表来自改造前**。新旧二进制不能直接读同一数据目录；
> 旧 `label_reverse`/`vprop_*` 目录会被新二进制拒绝，需重新 load 或迁移。

### 5.1 数据与配置

- 新格式数据：`vertex_data` 合并标签/属性；`vertex_existence`、`label_fwd` 保留。
- 数据用 sf0.1 Basic CSV + 适配 schema 重新装载（Basic 语料没有 Person 的 email/language 列；
  §2 的完整数据语义以旧库/neo4j 为准，本节只看引擎执行性能）。
- 8 compute、4 storage IO；3 轮交错，min。
- 邻接缓存默认关闭（`--adj-cache-capacity 0`）。

### 5.2 邻接缓存 A/B（cache N=8 vs 0）

同一份带 cache 数据的两次查询，仅切换 server 的 `--adj-cache-capacity`：

| 查询 | cache 8 min (ms) | cache 0 min (ms) | cache/0 |
|---|---:|---:|---:|
| complex-2 | 18.61 | 19.38 | 1.04 |
| complex-3 | 202.32 | 205.48 | 1.02 |
| complex-4 | 9.56 | 10.39 | 1.09 |
| complex-5 | 554.85 | 547.34 | 0.99 |
| complex-6 | 163.27 | 165.57 | 1.01 |
| complex-7 | 6.13 | 5.42 | 0.88 |
| complex-8 | 4.83 | 4.46 | 0.92 |
| complex-9 | 737.26 | 720.86 | 0.98 |
| complex-10 | 43.68 | 42.00 | 0.96 |
| complex-11 | 9.35 | 8.80 | 0.94 |
| complex-12 | 426.18 | 430.45 | 1.01 |

**结论：这项 out-only 缓存对 sf0.1 LDBC 复杂查询没有可测收益，部分查询因额外读 blob 略慢。**
因此默认关闭；保留存储/接口用于后续按需接入（见 §5.4）。

### 5.3 改造后 complex 子集（cache 0，vs neo4j）

| 查询 | eugraph min (ms) | neo4j min (ms) | 状态 |
|---|---:|---:|---|
| complex-2 | 17.27 | 5.41 | 🔴 慢 3.2× |
| complex-3 | 184.09 | 29.69 | 🔴 慢 6.2× |
| complex-4 | 8.69 | 5.27 | 🟡 慢 1.6× |
| complex-7 | 5.02 | 4.61 | 🟢 慢 1.1× |
| complex-8 | 3.65 | 4.32 | 🟢 快 1.2× |
| complex-10 | 38.65 | 16.92 | 🔴 慢 2.3× |
| complex-11 | 7.58 | 5.75 | 🟡 慢 1.3× |
| complex-12 | 372.19 | 103.35 | 🔴 慢 3.6× |

> 这是改造后同一轮的相对值，不对旧轮次做归因；CPU 频率/数据 schema 与 §2 不同。

### 5.4 结论

1. `vertex_data` 合并已完成：少掉 `label_reverse` 和所有 `vprop_*` 表，标签/属性共享 B-tree；
   但当前 complex 子集没有看到相对旧格式的提速，需要用同机旧二进制做正式 A/B 才能判断。
2. out-edge cache 在 sf0.1 LDBC 上没有收益，默认关闭；仅在明确需要 OUT 邻接且顶点已物化的
   专用路径再启用（接口已预留）。
3. 写路径注意：cache blob 是每顶点单记录，多连接同时更新同一顶点会产生 WT 写冲突；
   loader 用 4 连接 + cache 开启时实测会 commit 失败，需单连接或关闭 cache。
