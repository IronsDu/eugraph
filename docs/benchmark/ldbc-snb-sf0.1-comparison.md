# LDBC SNB Interactive SF0.1 对照（最新状态）

> 只保留当前结论：可运行性、最近一次有效耗时，以及剩余性能缺口的原因/思路。
> 历史轮次的过程数据已删除；需要复现时以本文的查询参数和测量口径为准。

## 1. 口径

- 数据：LDBC SNB Interactive **sf0.1**，eugraph 官方加载口径。
- 环境：Release；complex 8 compute / 4 storage IO；update 为一次性数据副本、4 compute、4G cgroup。
- 客户端：官方 Bolt driver，每条查询独立连接；eugraph 的 `id` 是整数，neo4j 是字符串。
- 同机交错 A/B；本机 CPU 频率会漂移，绝对值只用于同一轮参考，跨轮次看数量级。
- shortest 类参数：complex-1 `personId=933, firstName=Jose`；complex-13/14 `person1Id=933, person2Id=1129`
  （官方默认 ID 不在本地 sf0.1，使用真实存在的端点验证，结果已与 neo4j 对照）。

## 2. 可运行性与耗时

### 2.1 Short reads

| 查询 | eugraph (ms) | neo4j (ms) | 状态 |
|---|---:|---:|---|
| short-1 | 1.00–1.11 | 1.3 | ✅ |
| short-2 | 4.07–4.49 | 2.3 | ✅ 慢 1.8–2.0× |
| short-3 | 1.13–1.47 | 1.6 | ✅ |
| short-4 | 0.8–1.2 | 76.8–78.4 | ✅ 快 ~70–98× |
| short-5 | 0.9–1.2 | 162.2 | ✅ 快 ~130–180× |
| short-6 | 1.5–1.6 | 78.3 | ✅ 快 ~50× |
| short-7 | 1.7–1.9 | 122.1 | ✅ 快 ~64× |

### 2.2 Complex reads

| 查询 | eugraph (ms) | neo4j (ms) | 状态 |
|---|---:|---:|---|
| complex-1 | 20.5 / 21.1 | — | ✅（shortestPath） |
| complex-2 | 13.73 / 15.41 | 4.67 / 4.85 | ✅ 慢 2.9× |
| complex-3 | 131.5 / 136.7 | 26.63 / 27.10 | ✅ 慢 4.9× |
| complex-4 | 7.59 / 8.28 | 3.95 / 4.58 | ✅ 慢 1.9× |
| complex-5 | 475.6 / 488.4 | 90.07 / 92.70 | ✅ 慢 5.3× |
| complex-6 | 550–605 | 61–87 | ✅ 慢 ~9×（最近有效测量） |
| complex-7 | 4.82 / 5.53 | 4.27 / 4.72 | ✅ 慢 1.1× |
| complex-8 | 3.53 / 3.78 | 3.74 / 4.02 | ✅ 快 0.94× |
| complex-9 | 3960 | 37 | ✅ 慢 ~107×（最近有效测量） |
| complex-10 | 30.44 / 31.62 | 9.26 / 9.87 | ✅ 慢 3.3× |
| complex-11 | 7.02 / 7.79 | 4.16 / 4.73 | ✅ 慢 1.7× |
| complex-12 | 337.0 / 348.5 | 97.98 / 104.01 | ✅ 慢 3.4× |
| complex-13 | 4.4 / 4.6 | — | ✅（shortestPath，length=3） |
| complex-14 | **86000** | **2900** | ✅（首次跑通；PC 全表扫描，见 §3） |

### 2.3 Updates

一次性副本、4 compute、4G cgroup；只证明可完成，不是性能基准。

| 语句 | eugraph (ms) | 状态 |
|---|---:|---|
| update-1 | 258.6 | ✅ |
| update-2 | 39.2 | ✅ |
| update-3 | 28.7 | ✅ |
| update-4 | 36.4 | ✅ |
| update-5 | 14.5 | ✅ |
| update-6 | 43.6 | ✅ |
| update-7 | 573.8 | ✅ |
| update-8 | 10.3 | ✅ |

**当前没有不可运行的 LDBC 语句。** complex-5 不再挂死；complex-14 已跑通且结果与 neo4j 完全一致。

## 3. 剩余性能点、原因与思路

| 优先级 | 查询/范围 | 差距 | 已定位原因 | 下一步思路 |
|---|---|---|---|---|
| P0 | complex-14 | 86.0 s vs 2.9 s（~30×） | PC 子计划为 `AllNodeScan(a) → Expand → Expand → Expand → Filter`；`a.id = startNode(r).id` 只在末尾过滤，没变成索引 seek；每个 `r` 都从全部 Person 扫起 | 把 PC 的端点等式（OR 两方向）下推为索引 seek，或拆成两个 PC 分支做 union；消除 `AllNodeScan(a)` |
| P1 | complex-9 | ~107× | 尚未做最新剖析；长链 VLE/展开和中间结果规模是主要嫌疑 | 先补 plan + profiling，确认是扫描、展开还是属性读取；再决定 VLE 剪枝/索引 |
| P1 | complex-6 | ~9× | 同上；当前只有最近有效测量，没有最新热点 | 先 profile，再按热点处理 |
| P1 | complex-5 | 5.3× | 低 degree 的 `post→HAS_CREATOR` 访问放大：大量 source 各读 1 条边，全局 `edge_index` 的 per-source 定位成本占主导 | 增加按 (label, direction) 的邻接索引/边表局部性，避免每个 source 重新定位 B+Tree |
| P1 | complex-3 | 4.9× | `Tag(name)` 起点选择、`HAS_CREATOR` 方向的前向/反向表选择仍不理想 | 结合基数估计选起点；按方向选择更合适的边表 |
| P2 | complex-12 | 3.4× | `TagClass → Tag → Post` 多跳展开与中间结果规模 | 优化展开顺序/中间结果裁剪 |
| P2 | complex-10 | 3.3× | VLE 之后的 `IS_LOCATED_IN` / `HAS_CREATOR` 展开顺序 | 调整 join/expand 顺序，减少中间行 |
| P2 | short-2 | ~1.9× | 小查询固定开销 + 属性/列表构造 | 低优先级，收益有限 |

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
