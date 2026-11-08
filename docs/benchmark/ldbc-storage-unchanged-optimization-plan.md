# 存储结构不变前提下的 LDBC 执行层优化计划

> **状态**：第 1 步方案（未改代码）。
> **2026-11-04 更新**：执行层第一批（evaluator/aggregate/scan callback）已落地；最新读基准
> 与同族剩余候选见 [comparison §2/§3.10](ldbc-snb-sf0.1-comparison.md) 与本文 §A.8。
> **日期**：2026-11-03。
> **范围**：LDBC SNB Interactive sf0.1 的 complex/short 读查询，目标是缩小与 neo4j 的
> 同轮耗时比值。
> **硬约束**：
> 1. **不改 KV 表/key 编码**，不做 packed vertex record、packed adjacency、CSR 等存储布局改造；
> 2. **不加二级索引**。二级索引不属于 KV 布局改造，但当前对照的 neo4j 基线没有额外索引，
>    加了就破坏同 schema 公平对比，因此本轮排除（见 §8）。
> 3. 允许：binder/planner/优化器、物理算子、批处理、内存缓存、物化边界、loader/schema
>    元数据（仅限不新增索引的元数据，且需单独确认）。
> **依据**：[LDBC SF0.1 对照](ldbc-snb-sf0.1-comparison.md)、
> [计划质量分析](ldbc-plan-quality-analysis.md)、
> [存储 vs neo4j 差距分析](storage-vs-neo4j-gap-analysis.md)、
> [Storage 操作成本](storage-operation-costs.md)、
> [查询性能排查笔记](query-performance-optimization-notes.md)、
> [c5/c9 计划分析](../query/ldbc-c5-c9-plan-analysis.md)、
> [物化边界设计](../query/engine/query-engine-design.md#L766-L806)、
> [延迟标签过滤设计](../query/engine/deferred-label-filter-design.md)。

---

## 1. 结论摘要

在“不改 KV 布局、不加二级索引”的前提下，仍有明确执行层空间的是：

| 优先级 | 查询 | 当前（ms / 比值） | 主要手段 | 粗估收益 |
|---|---|---|---|---|
| **P0** | **c14** | 78.6 / 5.7× | 两个 weight PC 分支合并/公共前缀 CSE；边界 id 提升 | 78 → **50–70**（合并后可更低） |
| **P1** | **c5** | 200.4 / 2.3× | 批量边属性读；friends 集合下推；半连接/反向展开（高风险） | 200 → **170–185**（前两项）；**140–160**（半连接） |
| **P2** | **c10** | 18.3 / 1.8× | PC 批量化已落地；剩余去相关/展开顺序 | 18 → **13–16** |
| **P2** | **c6** | 33.0 / 1.3× | tag-anchored reorder 已落地 | 已达标；剩余 label/存储 |
| **P2** | **c12** | 119.6 / 1.4× | VLE 闭包缓存/早剪枝，减少 65k 中间行 | 120 → **105–115** |
| P3 | c1/c4/c7/short-2 | 4–11 / 1.8–3.1× | 算子/固定开销微调 | < 10 ms，ROI 低 |
| — | **c9/c3** | 73.2/2.7×、69.4/2.6× | 无索引、无 pack 时只剩边端点标签约束等 ~10 ms 级项 | 基本停在存储地板（见 §6.6） |

**总体判断（2026-11-08 最终版）**：14 条 complex 合计 **628.2 ms vs neo4j
292.2 ms（2.15×）**；c14/c5/c9/c3 四条仍是主要绝对差距来源。c5/c12/c9/c3/c14
五条仍占总耗时约 **80%**、占总差距约 **80%**。c6 reorder 与 PC 批量化已落地后，
剩余空间主要在 c14 的两个 PC 分支合并、c5 的边扫描、c9/c3 的无索引存储地板；
**不追求 1×**，c9/c3 除非改布局或加索引（本轮都不做）。

---

## 2. 口径与约束

### 2.1 测量口径

- 只比较**同轮** eugraph/neo4j 比值。本机 CPU 频率可让同一二进制差约 2×；
  19:57 的 2.0 GHz powersave 轮次不并入性能表。
- 读查询用 Release（`-O3 -DNDEBUG`），8 compute / 4 storage IO / 1 GB WT cache /
  `--wt-txn-sync none`；neo4j 1 GB heap / 512 MB pagecache。
- 同机交错 15 轮 + 3 warmup，两引擎各执行一次，取 min；两棵二进制/两份同源 data copy
  时正常/对调各一轮。
- 结果先对 neo4j：`scripts/verify_ldbc_results.py` 21/21；c3/c4 当前参数下两边都是 0 行；
  c14 只有 `pathWeight=17.0` 并列时输出顺序不同（同多重集）。

### 2.2 数字标注

- “实测”引自仓库已有 A/B 或 PROFILE；
- “粗估”是本计划的工程估算，必须用同机 A/B 复核，不能当承诺；
- 绝对值受频率影响，跨轮次只应看比值。

---

## 3. 当前基线

数据取自 [comparison §2.2](ldbc-snb-sf0.1-comparison.md#L80-L99)（2026-11-02 全量复跑；
c9 为 §3.9 优化后的值）：

| 查询 | eugraph (ms) | neo4j (ms) | 比值 | 绝对差 (ms) | 主导层（现有证据） |
|---|---:|---:|---:|---:|---|
| complex-14 | 78.64 | 13.80 | 5.7× | 64.8 | 计划冗余（PC 公共前缀）+ 每行 PC 求值 |
| complex-1 | 11.24 | 3.57 | 3.1× | 7.7 | 算子/后处理（shortestPath 后 OPTIONAL/collect） |
| complex-7 | 5.52 | 1.77 | 3.1× | 3.8 | 大结果集展开 + `head(collect())` 聚合 |
| complex-9 | 73.20 | 27.12 | 2.7× | 46.1 | 存储（53k expand + 53k coalesce） |
| complex-3 | 69.42 | 26.30 | 2.6× | 43.1 | 存储（52k message 属性读；0 行也全扫） |
| complex-4 | 5.60 | 2.17 | 2.6× | 3.4 | 扫描/过滤（0 行）；未拆解 |
| complex-5 | 200.35 | 85.87 | 2.3× | 114.5 | 存储（89k posts × 2 expands） |
| complex-8 | 3.19 | 1.57 | 2.0× | 1.6 | 算子/小绝对 |
| complex-11 | 4.88 | 2.65 | 1.8× | 2.2 | 多跳展开/属性读取 |
| complex-10 | 18.30 | 10.31 | 1.8× | 8.0 | PC 批量化后；剩余为 PC 展开 |
| complex-2 | 4.30 | 2.44 | 1.8× | 1.9 | 算子/属性物化 |
| complex-12 | 119.57 | 88.43 | 1.4× | 31.1 | VLE 结果流（dispatch 已批量化） |
| complex-6 | 33.02 | 25.34 | 1.3× | 7.7 | tag reorder 后；label 检查/存储 |
| complex-13 | 0.99 | 0.90 | 1.1× | 0.1 | shortestPath，已接近 |

> 注意：`comparison §3` 的“剩余性能点”表还是优化前数字（c14 144.1/15.95、c10 52.6/9.49），
> 与 §2.2 不一致；以 §2.2 为准。c10 的比值在不同轮次从 4.8×→5.5×→7.8× 漂移，
> 是表里最不稳定的一个，必须复测后再作为目标。

---

## 4. 差距来源（只保留与本计划相关的部分）

### 4.1 存储访问模型（c5/c9/c3 的地板）

- 一条属性一行、一条边两行 `edge_index`、边属性另表；Expand 每 source 一次
  `search_near`，coalesce 每顶点一次前缀下降 + 扫全部属性行（Message 4–5 行）。
- 实测每次 edge/属性访问 **1–2 µs**，neo4j 摊算 **0.3–0.5 µs**；单存储层差 **3–6×**
  （[storage-gap §5.2](storage-vs-neo4j-gap-analysis.md#L440-L456)）。
- WT 本身不慢：`search` 567 ns、`search_near` 411 ns、key decode 7–19 ns
  （[storage-operation-costs §2](storage-operation-costs.md#L57-L67)）。慢在“每条记录走一遍
  seek→next→decode→回调→Value→算子”，以及 label/属性/邻接没有内联进 node/rel record。
- c9 的一次采样里 **48.8%** 在 WiredTiger 行扫描
  （[value-copy 结尾](query-value-copy-profiling.md#L1969-L1974)）。
- 只改布局、不改访问范式，收益约中性：labels+属性并表 c5 约 −8%、c14 约 −3.4%；
  N=64 邻接缓存反而 c5 +18%、c14 +10%（[storage-gap §0.1](storage-vs-neo4j-gap-analysis.md#L39-L58)）。
  **所以本计划不碰布局，只做“减少访问次数/减少每行管线”的执行层优化。**

### 4.2 compute/IO dispatch（已大部分批量化）

- 真实跨线程 dispatch 约 **10 µs**，是 WT seek 的 ~20 倍；未批量化时是灾难。
- 已兑现：c12 VLE 47,787→192 dispatch，735→152 ms；c14 91,672→696，291→119.5 ms。
- 本计划只处理**残余的逐条调用**，主要是 c5 的 29,894 次边属性读（见 §6.2）。

### 4.3 计划层（c14/c10/c5/c6 的剩余空间）

- 无 CBO/度数统计，RBO 选 join 顺序；N=0 时没有 `state.count`。
- c14：两个 PC 各跑一遍相同前缀；4 条路径 12 条关系、去重后 8 个人，24 次相关 apply；
  邻居缓存只在**分支内**生效（dbHits 125k→57.7k），跨分支仍重复
  （[plan-quality §5](ldbc-plan-quality-analysis.md#L75-L88)）。
- c5：`friend IN friends` 在 89,415 条 post 和 89,415 条 creator 边全部展开后才过滤到 262 行；
  缺少半连接/下推（[plan-quality §4](ldbc-plan-quality-analysis.md#L61-L73)）。
- c10：文档只有“VLE 后 `IS_LOCATED_IN`/`HAS_CREATOR` 展开顺序”一句，尚未逐算子归因。
- c6：`Expand→Expand`，没有 PE，延迟 label 过滤挂不上，仍付 13.3 ms 的 `:Post` 检查。
- 弱属性：非 CREATE 变量扫全 catalog 标签，`id` 得到 14 个候选；PE 夹在 Filter 与 LabelScan
  之间，索引规则看不进去（10-26 记录；11-02 GDB 仍见 `evalDynamicPropertyRef`，需复核）。

### 4.4 算子层（已大部分修掉）

- c3：batch 去重 + 共享 `VertexValuePtr` + 无属性边跳过 eprop，490→136 ms。
- c14：动态属性只读视图，158→134 ms；邻居缓存，154→94 ms。
- c6：prefetch 快路径，85→69 ms，Filter self CPU 25→2.2 ms。
- 剩余：c14 每分支 Filter/PE、逐行 `Value`/variant、`vector<BatchedEdge>` 中间拷贝、
  `std::function` 回调。**不要再往“缩小 Value/内存布局”投入**（152→112 B 在 LDBC 上零收益）。

---

## 5. 优化项总表

> 收益为“粗估”，全部需要同机 A/B；风险按实现复杂度 + 语义面评估。

| 编号 | 查询 | 手段 | 预期 | 风险 | 依赖 |
|---|---|---|---|---|---|
| **A1** | c14 | 语句级共享邻居缓存（跨 PC/Expand） | −10~15 ms | 中低 | 只读、单一快照 |
| **A2** | c14 | 两个 PC 公共前缀 CSE / PC 去相关 | 85 → ~50 ms | 中高 | planner/CSE、TCK |
| **A3** | c14 | path 变量静态标签传播 + IndexScan 穿透 PE | −5~15 ms | 中 | binder/planner |
| **B1** | c5 | 批量边属性读 API（`membership.joinDate`） | 41.2 → ~20 ms | 低 | 新增 batch API |
| **B2** | c5 | `friends` 集合下推 Expand dst | −10~15 ms | 中 | 复用 `allowed_dst_values_` |
| **B3** | c5 | 反向 `HAS_CREATOR` + forum 半连接 | −30~40% seek | 高 | 基数估计/受保护规则 |
| **C1** | c10 | 先 PROFILE，再定去相关或展开顺序 | 50 → 35–40 / 15–25 | 中 | 先测量 |
| **D1** | c6 | 延迟 label 过滤扩展到 `Expand→Expand` | 已尝试后回退：c10/c14 回归；c6 改由 tag reorder 解决 | 低中 | 回退（deferred design §9） |
| **E1** | c12 | VLE 闭包缓存/早剪枝 | 147 → 130–140 | 中 | 先 PROFILE |
| **F1** | c9/c3 | 边端点标签约束（条件项） | c9 ~10 ms、c6 ~8 ms | 中 | loader/schema 元数据 |

---

## 6. 逐项方案

### 6.1 P0：complex-14（78.6 ms / 5.7×）

**现状与证据**

- 两个 PC 分支各约 42 ms：Expand1/2/3 合计 ~17–18 ms、coalesce(PE) ~6 ms、Filter ~7.5 ms、
  anchor PE ~5.5 ms（[plan-quality §5](ldbc-plan-quality-analysis.md#L75-L88)）。
- 4 条路径 12 条关系、去重后 8 个人，`CorrelatedVertexProbe` 发射 24 个锚点；同一个人
  `a←HAS_CREATOR←comment` 的展开被重复最多 4 次；两个 PC 的相同前缀又各跑一遍。
- 邻居缓存已把**分支内**重复消掉（dbHits 125k→57.7k），但**跨分支**仍重复。
- `allShortestPaths` 已批量化 BFS，约 16 ms；anchor LabelScan + CorrelatedVertexProbe 已落地。

**A1：语句级共享邻居缓存（先做，低–中风险）**

- 把 `ExpandPhysicalOp` 的邻居缓存从 per-instance 提升到语句级（`QueryContext` 或
  `StatementCache`），key = `(src_vertex_id, direction, edge_labels, dst_labels)`，
  两个 PC/多个 Expand 共享。
- 共享容器放语句级；启用条件仍沿用现有的“只读 + PC 右子计划（`correlated_subplan_depth > 0`）”，
  这样不改变非 PC 查询的行为；命中/未命中混合时按原 edge-major 顺序重组；
  `dst_bound`/`edge_bound`/allowed-filter/undirected 拆分路径不启用；上限 64K 边/64K 源。
- 预期：−10~15 ms（文档估算）。
- 风险：缓存生命周期/内存/跨协程可见性；只读门控；顺序不能变。
- 测试：新增“两个 PC 共享同一 anchor 的展开只发生一次”用例，断言结果逐行一致 +
  共享缓存命中计数 > 0（旧代码必失败）；21/21；TCK。

**A2：PC 公共前缀 CSE/去相关（中–高风险，收益最大）**

- 两个 PC 的 `a←HAS_CREATOR←comment→REPLY_OF` 前缀完全相同；可做语句内 CSE（结构完全
  匹配才共享），或把 PC 改成对 path 端点的 join/去相关。
- 文档估可到 **~50 ms**（3.5×）。
- 风险：PC 的 list/unwind/pattern 管线会消费“没有任何 slot 引用命名”的表示
  （如 `collect(post)` 里已构造的 post）；2026-10-29 的 Filter 边界因此对 PC 整体关闭。
  CSE 必须保留 PC 的逐行 list 语义；需要 TCK + 逐行结果验证。
- 建议：A1 先落地并量出剩余；A2 单独立项，先做结构匹配守卫，不匹配就回退。

**A3：path 变量静态标签传播（中风险，顺带）**

- `bind_expression.cpp` 对非 CREATE 变量扫全 catalog 标签，`id` 得到 14 个候选；
  path 变量 `a`/`b` 在查询里静态是 `Person`，本应只剩一个候选。
- 目标：为 pattern/path 变量传播已知标签，把弱属性 candidates 裁到交叠标签；
  单标签时生成强 accessor；让索引规则能穿透 PE/Project（或在插 PE 之前尝试
  `Filter(LabelScan)→IndexScan`）。
- 先复核：用 `EXPLAIN`/PROFILE 确认 c14 端点是否仍走 `LabelScan`、`b.id` 是否仍多候选。
  10-26 记录过 14 候选；11-02 GDB 仍见 `evalDynamicPropertyRef`，说明动态求值路径没完全消失。
- 预期：−5~15 ms（PC 每行属性解码），不确定，需 A/B。
- 风险：标签传播错误会改变语义；`tryExtractBoundCondition` 目前用 `candidates[0]`，
  多候选时可能选错标签；必须加回归。

**验证**：PC 结果逐行一致；共享缓存命中计数；21/21；全量 ctest + TCK。

---

### 6.2 P1：complex-5（182.0 ms / 2.1×）

**现状与证据**（[plan-quality §4](ldbc-plan-quality-analysis.md#L61-L73)）

> 下表成本来自 plan-quality 的 c5 194 ms 轮次；当前基线是 182 ms，绝对数略有缩放，
> 但各项比例与归因不变。

| 动作 | 成本 |
|---|---:|
| forum → CONTAINER_OF → post（89,415 行） | 23.0 ms |
| post → HAS_CREATOR → friend（89,415 行） | 76.7 ms |
| Filter `friend IN friends`（89,415 → 262） | 10.4 ms |
| `membership.joinDate` eprop（29,894 次精确 search） | 41.2 ms |

**B1：批量边属性读（先做，低风险）**

- 现状：`getEdgeProperties` 逐条调用；29,894 次、41.2 ms（约 1.38 µs/次）。
- 做法：新增 `batchGetEdgeProperties(label, eids)`（async/sync 各一层），复用一条 cursor/
  一次 dispatch 读多条 `eprop`；按输入顺序回填，缺失属性返回 nullopt；只影响 c5 的
  `membership.joinDate` 这类边属性过滤，不改 KV。
- 预期：41.2 → ~20 ms（0.6–0.7 µs/次），c5 省 **15–20 ms**。
- 测试：批量与逐条结果逐值对拍（多属性、缺失、重复 eid）；c5 21/21；TCK。

**B2：friends 集合下推 Expand dst（中风险）**

- 复用 `allowed_dst_values_` / `ExpandAllowedFilterContext`：把 `friends` 的 VertexId 集合
  下推给 `Expand(post→friend)`，扫描时先按 dst 集合过滤，再决定是否 emit。
- 注意：**它只减少 89k 输出行和后面的 Filter，不减少 89k 边扫描**；预计 10–15 ms，
  不是数量级。
- 若 `friends` 是运行时 `Aggregate(collect(distinct friend))` 的结果，需要 Apply 式注入或
  共享 hash set；保持 OPTIONAL/重复语义。
- 测试：`friend IN friends` 结果一致；重复/空集合；OPTIONAL 左行保留；21/21；TCK。

**B3：反向展开/半连接（高风险，单独立项）**

- 从 174 个 friend 反向 `HAS_CREATOR`，再与 forum 的 posts 做半连接/哈希连接；
  文档估 **30–40% 的 seek 减少**。
- 但手工 join reorder 曾实测退化到 **35 s+**（CrossProduct 灾难）；必须用受保护的规则 +
  基数估计（friends 基数、forum/post 计数），不能直接改顺序。
- 预期：若成功，c5 到 **120–140 ms**（1.4–1.6×）；失败则回退。
- 建议：B1+B2 落地并复测后再决定是否投入 B3。

**先确认**：29,894 条 membership 是否都已经是 friends 的边；如果不是，先把更便宜的
`friend IN friends` 排到 `joinDate` 前面，可能直接省掉大部分 eprop 读。

---

> **2026-11-08 更新**：PatternComprehensionApply 已按 batch key 批量化
> （[pattern-comprehension-batching](../query/engine/pattern-comprehension-batching.md)）。
> sf0.1 同机交错 A/B：c10 **41.2 → 20.4 ms（2.02×）**，最终 15 轮表 **18.3 ms**；
> c14 最终 **78.6 ms**（批量化对其无收益，实际 pattern 展开占主导）；c5 对照 219→222 ms。
> c10 结果哈希不变。A1 语句级共享邻居缓存已实现并 A/B：跨两个 weight 分支只命中少量
> 首/末级 Expand，c14 84→82 ms，无净收益，已回退。
>
### 6.3 P1：complex-10（18.3 ms / 1.8×）

**现状**：当前最终版 18.3 ms / 1.8×（PC 批量化后）；历史上比值从 4.8×→5.5×→7.8×
漂移，是表里最不稳定的。10-22 的旧剖析说 `size([x IN posts WHERE ...])`
的推导是每帖驱动一次、占 88%，但那是**秒级旧版本**，不能直接套到当前 18 ms。

**第一步：测量（不改代码）**

- `EUGRAPH_OP_TRACE=1` + `EUGRAPH_IO_TRACE=1` + PROFILE；对 neo4j 逐算子比
  rows/dbHits；重点看：
  - `VarLenExpand` 输出行数；
  - `IS_LOCATED_IN` / `HAS_CREATOR` 的 rows、self time；
  - `PatternComprehensionApply` / `Unwind` 的执行次数（是否 4,086 次）；
  - `Aggregate` 的输入行数；
  - `wtSearchNear` / `scanRows` / `ioDispatches`。
- 判据：如果 `PatternComprehensionApply` 的 apply 次数 ≈ posts 数、self time 占大头 → H1；
  如果 `Expand` 的中间行数远大于 neo4j、Filter 位置靠后 → H2。

**H1：per-post 相关子计划**

- 方向：把 `size([x IN posts WHERE (x)-[:HAS_TAG]->()<-[:HAS_INTEREST]-(p)])` 去相关/
  半连接化——先物化 `p` 的 interest 集合（每个 p 一次），再对 posts 做集合查找；
  或在只用于计数时降级为 `count`/`existence`。
- 预期：50 → **15–25 ms**（2.3–3.9×），但需要语义守卫和 A/B。
- 风险：list comprehension 结果可能被外部消费；旧 `count_only` 曾无可测收益，不能假设一定有效。

**H2：展开顺序/中间行**

- 方向：把 country/date 过滤下推到 post 展开之前；`dst IN list`；或基于 label/edge 计数的
  局部代价选择；不要无统计地手工重排。
- 预期：50 → **35–40 ms**（5.5–6.2×）。

**不建议**：在没测出 H1/H2 之前直接改 join 顺序；历史手工 reorder 曾退化到 35 s。

---

### 6.4 P2：complex-6（63.6 ms / 2.3×）

**现状**：计划 `friends → HAS_CREATOR(53k) → :Post 过滤(17,739) → HAS_TAG(11,538)`；
`:Post` 标签检查 **13.3 ms**。Expand 后直接接另一个 Expand，没有 PE，所以
`setRequiredLabels` 挂不上，延迟 label 过滤没命中（[design §6](../query/engine/deferred-label-filter-design.md#L120-L126)）。

**D1：把延迟 label 过滤扩展到 `Expand→Expand`**

- 在 Expand 后插 **labels-only PE** 或行压缩 Filter：用 `{vid}` 前缀一次 `search_near`
  读 label（+ 后续属性），按 `required_labels` 压缩输出行；复用
  `required_labels` / `label_filter_cache` / `deferred_label_filter_count`。
- 预期：省 8–10 ms，63.6 → **53–55 ms**（约 2.0×）。
- 风险：低–中；不依赖 PE 已物化属性，需保证“标签判定在使用该顶点的所有分支之前”。
- 测试：`deferred_label_filter_count > 0`；Expand `scanRows` 从 188,893 降到 53,192；
  结果逐行一致；21/21；TCK。

**2026-11-07 结果**：D1 的 labels-only wrapper 因 c10/c14 回归已回退；
c6 改为 tag-anchored join reorder，knownTag 863 posts 作为驱动，sf1 c6 从
**2.12s 降到 56ms**（~38×），21/21 结果哈希一致。详见
[tag-anchored-c6-join-reorder](../query/engine/tag-anchored-c6-join-reorder.md)。

---

### 6.5 P2：complex-12（146.9 ms / 1.6×）

**现状**：VLE `HAS_TYPE|IS_SUBCLASS_OF*0..` 65,380 行；dispatch 已从 47,787 批量化到 192；
VLE 约 80 ms，上层 4,086 行展开；剩余是结果流，不是 dispatch。

**E1：先 PROFILE，再决定**

- 方向：如果 TagClass 层次有重复根，做 per-query 闭包缓存/早剪枝；或减少 VLE 输出到
  下游的中间行（标签/类型过滤、去重、聚合下推）。
- 预期：10–20%（15–30 ms），中等风险；不要先动 dispatch。
- 测试：VLE 行数/结果一致；21/21；TCK。

---

### 6.6 c9 / c3：无索引、无 pack 时的地板

**c9（110.2 ms / 4.1×）**

- 53k 次 `HAS_CREATOR` + 53k 次 coalesce；label + creationDate 已合并为一次 `{vid}` 前缀
  扫描（comparison §3.9），排序探测已做（comparison §3.3）；coalesce 热路径剩余
  53k 次下降（~26 ms）+ 6–9 行 `next`/decode。
- 无 pack、无索引时，只能做：
  - 边端点标签约束，省掉 2–4 行 label 读（~10 ms 量级，见 F1）；
  - 谓词/批量微调（收益有限）。
- **粗估地板 ~100 ms（3.5–4×）**，不会回到 1–2×。

**c3（98.3 ms / 3.9×）**

- 52k 条 message 的 label + `creationDate` 读；计划与 neo4j 同形；0 行也要全扫。
- 无索引时同样只能做 label 约束/批量微调；**地板接近当前值**。

**关于 `Message.creationDate` 索引**

- 技术上它不是 KV 布局改造，旧口径实测 c9 **160.6→71.4 ms**，c3 52k→787 行；
  但 neo4j 基线没有该索引，加了就破坏同 schema 公平对比，**本轮不做**（[notes §8.9](query-performance-optimization-notes.md#L347-L361)）。
- 如果未来切换为“产品优化/允许两边同索引”的口径，这是 c9/c3 最大的单点杠杆。

---

### 6.7 小查询（c1/c4/c7/short-2）

- **c1（13.65 ms / 3.7×）**：shortestPath BFS 已批量化（22→15 ms）；剩余是 OPTIONAL/collect
  后处理，绝对差 <10 ms；低 ROI。
- **c4（8.45 ms / 3.6×，0 行）**：扫描/过滤成本，未拆解；低 ROI。
- **c7（5.28 ms / 3.0×）**：大结果集展开 + `head(collect(...))`；可尝试把 `head(collect)`
  改写为 limit-1/存在性，但绝对收益 ~1–2 ms；低 ROI。
- **short-2（4.37 ms / 2.7×）**：固定开销/2 跳；低 ROI。

本轮不建议为这些查询单独投入；如果做 suite 级几何均值，可在最后顺手处理 c7。

---

## 7. 跨查询机制

1. **批量边属性读**（B1）：c5 直接受益；如果 c7 的 `LIKES.creationDate` 是边属性，也可受益。
   低风险，建议优先。
2. **语句级共享邻居缓存**（A1）：c14、c10；只读、单一快照、有界；中低风险。
3. **边端点标签约束**（F1，条件项）：如果 loader/schema 能声明 `HAS_CREATOR` 的 dst 是
   `Message`、`CONTAINER_OF` 的 dst 是 `Post`，planner 可省略冗余 label 检查；
   c6 ~8 ms、c14 ~3 ms、c9 ~10 ms。**这不是二级索引，但改 loader/schema 元数据，
   需要单独兼容性评审；若要求与 neo4j 完全同 schema，本轮不做。**
4. **轻量基数统计**：label 计数已有；再用边类型计数/列表基数做展开顺序选择，
   替代手工规则。c5/c10 受益，属于中期投入；不引入 per-vertex degree（N=0 没有）。
5. **`dst IN list` / 半连接**：c5/c10 的通用形态；先做行过滤（B2），半连接（B3）单独立项。

---

## 8. 明确不做

| 项 | 原因 |
|---|---|
| packed vertex record / packed adjacency / CSR | 改 KV 布局，超出本轮范围；收益见 [storage-gap §6.2](storage-vs-neo4j-gap-analysis.md#L729-L753) |
| **二级索引**（`Message.creationDate` 等） | 与 neo4j 同 schema 公平对比；neo4j 基线无额外索引 |
| WT cache/page size/evict 调参 | 只影响 I/O；warm-cache 的 0.4–0.6 µs CPU 下降成本不变 |
| key decode / `Value` 尺寸优化 | key decode 7–19 ns；152→112 B 在 LDBC 上零收益 |
| 继续共享 `scanEdgesBatch` cursor | c5 只 −13%；每源 `search_near` 仍在 |
| 无统计的手工 join reorder | 曾退化到 35 s+（CrossProduct 灾难） |
| 单方面加 `creationDate` 索引混进对比 | 仓库明确禁止；只能作为单独的产品优化口径 |

---

## 9. 验证与回归

### 9.1 性能 A/B

- 同一二进制内做 baseline/optimized（或两棵二进制、两份同源 data copy），
  交错 15 轮 + 3 warmup，取 min；正常/对调 data copy 各一轮。
- 只比较同轮比值；记录 p50；绝对值注明 CPU 频率。
- 每次只验证一个改动；大改动拆成单变量（例如 A1、A2 分开）。

### 9.2 计数器与 PROFILE

- PROFILE：`rows`、`dbHits`、`wtSearchNear`、`scanRows`、`selfCpu`，按算子看；
- `EUGRAPH_OP_TRACE=1`：算子 rows/chunks/self CPU；
- `EUGRAPH_IO_TRACE=1`：`io_dispatches`、`wt_search`、`wt_search_near`；
- 必要时 GDB 原生采样（`bt 60`，区分 compute/IO 线程）。

### 9.3 正确性

- `scripts/verify_ldbc_results.py` 21/21 与 neo4j 逐行一致；
- 全量 `ctest --preset debug`（含 TCK）；
- 每个优化新增“能区分对错”的测试：
  - A1：跨 PC 共享缓存命中计数 > 0，结果一致；
  - A2：CSE 前后逐行一致，PC list 语义不变；
  - B1：批量 eprop 与逐条逐值对拍；
  - B2：`friend IN friends` 重复/空集合/OPTIONAL 语义；
  - D1（已回退）：曾验证 `deferred_label_filter_count > 0`，但 c10/c14 回归；
  - C1：按 H1/H2 选定的计划形状/行数断言。

### 9.4 流程

按仓库 AGENTS：

1. **第 1 步**：本文件（方案对齐）→ 等待确认；
2. **第 2 步**：测试用例对齐 → 等待确认；
3. **第 3 步**：编码；
4. **第 4 步**：相关测试 + 全量 ctest；
5. **第 5 步**：通知开发者验证。

---

## 10. 执行顺序与开放问题

### 10.1 建议顺序

1. **c14 A1（共享邻居缓存）**：低–中风险、收益明确；落地后量剩余，再决定 A2。
2. **c5 B1 + B2（批量 eprop + friends 下推）**：低/中风险；B3 半连接单独立项。
3. **c10 测量**：先不改代码，用 PROFILE 判定 H1/H2；再决定去相关还是重排。
4. **c6 D1（延迟 label 过滤扩展）**：已尝试并因 c10/c14 回归回退；c6 由 tag reorder 解决。
5. **c12 E1（PROFILE 后）**。
6. **c9/c3**：除非允许边端点标签约束（F1），否则本轮不动；索引方案仅在公平口径改变时重启。
7. 小查询不投入。

### 10.2 开放问题（需要确认）

1. **边端点标签约束（F1）是否在范围内？** 它不是二级索引，但依赖 loader/schema 元数据；
   如果要求与 neo4j 完全同 schema，建议不做。
2. **c5 B3 半连接的风险是否接受？** 需要基数估计/受保护规则，可能单独立项。
3. **c10 的测量由谁做、时间盒？** 在 H1/H2 未分之前，不应承诺收益。
4. **c14 A2 的 CSE 是否本轮做？** 建议 A1 落地后再评估；A2 需要独立的语义守卫设计。

### 10.3 预期总收益（粗估，需 A/B）

| 项 | 节省 |
|---|---:|
| c14 A1+A3 | 15–30 ms |
| c14 A2（若做） | 再 15–25 ms |
| c5 B1+B2 | 25–35 ms |
| c5 B3（若做） | 再 30–50 ms |
| c10（H2 / H1） | 10–15 / 25–35 ms |
| c6 D1（已回退） | — |
| c12 E1 | 7–17 ms |

不含 B3/A2 的执行层组合约 **65–125 ms**（取决于 c10 是 H2 还是 H1），complex 总时间约
**655–715 ms（2.2–2.5×）**；含 B3/A2 可到 **580–670 ms（2.0–2.3×）**。
**c9/c3 不动，1× 不现实**；这与
[storage-gap §6.2](storage-vs-neo4j-gap-analysis.md#L729-L753) 的地板判断一致。

---

## 11. 参考

- [LDBC SNB SF0.1 对照](ldbc-snb-sf0.1-comparison.md) §2.2 最新全量表、§3.2/§3.9 等优化记录。
- [LDBC 计划质量分析](ldbc-plan-quality-analysis.md) §2–§5：c5/c6/c9/c14 逐算子对照。
- [存储 vs neo4j 差距分析](storage-vs-neo4j-gap-analysis.md) §5.2/§5.3/§6.2：per-access 成本与地板估算。
- [Storage 操作成本](storage-operation-costs.md)：WT seek / dispatch 单次成本。
- [查询性能排查笔记](query-performance-optimization-notes.md)：c3 复盘、Filter 边界、§8.9 索引公平性。
- [c5/c9 计划分析](../query/ldbc-c5-c9-plan-analysis.md)：c9 当前计划、c5 批处理/剩余成本。
- [物化边界设计](../query/engine/query-engine-design.md#L766-L806)：Filter/LIMIT 边界、LateMaterialize。
- [延迟标签过滤设计](../query/engine/deferred-label-filter-design.md)：c9 已落地、c6 后续扩展。

---

## 附录 A：Evaluator 临时对象与分配热点（2026-11-03）

> 这是 §4.4 "算子层" 的代码级取证和落地记录。设计细节见
> [evaluator-scratch-reuse-design.md](../query/engine/evaluator-scratch-reuse-design.md)。

### A.1 先校正一个说法

执行器**不是**纯 row-at-a-time：它有 `DataChunk`/`ColumnBuffer` 的 typed 列存、
`SelectionVector`、typed binary/unary 快路径。准确说法是：

> **列式外壳 + typed kernel，但关键路径仍有 row-wise fallback 和每 chunk/每元素重复分配；
> kernel 是标量循环（没有手写 SIMD）；没有 intra-query 并行。**

### A.2 代码证据

| 位置 | 问题 |
|---|---|
| `expression_evaluator.cpp` `evaluate` | 逐行 `getValue`/`setValue`，即使两边同类型；已有 `copyValueFrom` 未用 |
| `evaluatePredicate` | 逐行构造 `Value` 判 bool；BOOL 列可直接读 typed buffer |
| `eval_function_call.cpp` | 每次调用两个临时 `std::vector` |
| `eval_case.cpp` | 每次 CASE 一个 `branches` vector；逐行 `Value` |
| `eval_map.cpp` | 每次 MAP 一个 `entry_cols` vector；逐行 `MapValue` |
| `eval_property.cpp` | 行循环内 `std::vector<Value> found` |
| `eval_list.cpp` | 旧实现把元素求值放在行循环里：R×N 次整块求值 |
| `eval_list_comprehension.cpp` | 每元素构造 `DataChunk` + 拷贝全部输入列 + `vector<bool>` |
| `eval_quantifier.cpp` | 每元素构造 `DataChunk` |
| `eval_reduce.cpp` | 每元素构造 `DataChunk`；每元素重建 overrides map |
| `aggregate_physical_op.cpp` | 每 chunk `vector<vector<Value>>` / `vector<vector<vector<Value>>>` + 逐行 `Row` |
| `temp_columns_` | `deque` 只 push_back 不复用；Aggregate/Sort 跨 chunk 复用会一直增长 |
| evaluator 生命周期 | Filter/Project 每 chunk 新建；Aggregate/Sort 跨 chunk 复用但不 reset |

### A.3 已落地的第一批

- `ColumnBuffer::reset` / `Column::resetFlat` / `resetConstant` / `copyFrom` / `getBool`；
- `ExpressionEvaluator` 按求值深度分帧的 `EvalScratch`（函数参数、CASE 分支、MAP/LIST 条目、
  单行谓词、单行 chunk）；
- 临时列池 `temp_columns_` + `temp_used_` + `beginExpression` / `resetForChunk` /
  `tempMark` / `resetToTempMark`；
- `evaluate` typed copy、`evaluatePredicate` BOOL 快路径；
- `evalList` 从 O(R×N) 降为 O(N)；
- list comprehension / quantifier / reduce 复用单行 chunk，按元素回收临时列；
- `evalProperty` 单候选零分配；
- Filter/Project/evaluator 提到 chunk 循环外，Aggregate/Sort 每 chunk reset。

### A.4 后续与边界

- **已做**：Aggregate typed arg 路径（chunk 级 typed 列 + `AggregateInput` 行视图，
  去掉 `vector<vector<vector<Value>>>`）；存储 scan callback 的 `std::function` →
  `folly::FunctionRef`；`evalCase`/XOR/IN 的逐行 Value 物化清理。
- **未做**：per-group batch aggregate update、typed GROUP BY key、手写 SIMD、
  intra-query 并行（本轮明确不做；只利用编译器自动 SIMD）。

### A.5 验证口径

- 新增 `EvaluatorScratchTest` 覆盖复用、typed copy、BOOL 谓词、list literal、跨 chunk reset；
- 既有 `evaluator_typed_tests` / `query_executor_tests` / 全量 ctest + TCK；
- 性能用同机交错 A/B + allocation counter/GDB 采样，重点 c7/c10/c14/c3；c5/c9 存储受限，
  不作为 evaluator 收益的判据。

### A.6 落地与验证状态（2026-11-03）

- 代码：`ExpressionEvaluator` 分帧 scratch、临时列池、typed copy/BOOL 快路径、
  `evalList` O(R×N)→O(N)、list comprehension/quantifier/reduce 单行 chunk 复用、
  `evalProperty` 单候选零分配、Filter/Project/Aggregate/Sort evaluator 复用。
- 构建：`cmake --build --preset debug --target evaluator_typed_tests` 通过。
- 测试：
  - `evaluator_typed_tests` **30/30**（含新增 `EvaluatorScratchTest` 5/5）；
  - `query_executor_tests` 的 list comprehension / reduce / quantifier 组 **7/7**；
  - 全量 `query_executor_tests` **614/614** 通过（含新增 aggregate typed 用例）；
  - `data_chunk_tests` **20/20**（`CellAccess` 3/3）；
  - storage callback：`wt_tests` 7/7、`index_store_tests` 13/13、
    `metadata_tests` 20/20、`index_e2e_tests` 56/56；
  - 全量 ctest/TCK 尚未在本轮执行（构建/数据成本高），提交前需按 AGENTS 补跑。
### A.7 Rebase main #288 后的增量优化

- `ab93482` 引入 vertex summary cache、`scanEdgesExact`、SemiJoin/AntiSemiJoin 批量右子计划、
  ProjectionExtract persistent ctor cache；本分支 rebase 后：
  - `scanEdgesExact` 的同步 callback 也统一为 `folly::FunctionRef`；
  - Expand 的 exact seek 和普通 dst 过滤改用 `columnVertexId`，避免每行 Value；
  - SemiJoin 批量右键从 `vector<vector<VertexId>>` 改为扁平 `VertexId` 数组 + `span` 视图，
    右侧扫描使用 `columnVertexId`；
  - aggregate 参数 typed 策略收紧为仅 `sum`/`avg` 的数值参数；`count/min/max/collect/
    percentile` 保持 ANY，避免 `max([1,2.0,5])` 被归一化成 5.0、`collect(x)` 被强转。
- 这轮同时修正了两个由 typed 化引出的 TCK 回归：ANY 列 `getInt64` 回退、普通 collect
  不保留 null（内部 pattern/list comprehension 仍显式保留）。
- 注意：调试构建目录曾出现 `libeugraph_compute.a` 被中断构建截断（8 字节），
  导致 query_executor_tests 链接缺 `CypherQueryParser` 符号；删除该归档并重建
  `eugraph_compute` 后恢复。这不是源码问题，但复现构建失败时先检查静态库大小。
- TCK（2026-11-04，rebase main ab93482 后）：**3930/3983**，唯一失败为
  `Temporal2 [6]` 的既有日期时区用例（与本轮改动无关），52 个 undefined step；
  受影响 aggregate/mutation/precedence 子集 178/178；新增 aggregate-typed-regressions 6/6。
- loader 基线 `loader_progress.cpp` 的 unused-function `-Werror` 已修复；
  `loader_progress_tests` 8/8、`loader_integration_tests` 42/42。
- 尚未做：同二进制性能 A/B、per-group batch aggregate、typed GROUP BY key、手写 SIMD、
  intra-query 并行；全量 ctest 中的 loader/integration 目标仍受基线
  `loader_progress.cpp` unused-function `-Werror` 影响。

### A.8 2026-11-04 Release 复测与同族剩余点

- 分支 `perf/evaluator-scratch-reuse` @ `9dd10b24`，Release 重新构建，LDBC sf0.1 读基准：
  complex 合计 **637.0 ms vs neo4j 269.8 ms（2.36×，全量预热稳态）**；启动后首轮为
  633.5 vs 299.9（2.11×）。eugraph 总量相对 2026-11-03 的 702.2 ms 下降约 **9.3%**；
  c10/c14/c5/c12/c9/c3/c1 分别下降 10.2/8.4/9.8/14.2/6.9/5.0/12.8%（跨轮，非同二进制 A/B）。
  短查询的绝对值也下降，但“谁更快”受 neo4j 预热影响，见 comparison §2.4。
- 21/21 结果一致；详细表、口径与 PROFILE 归因见 comparison §2/§4/§5.8。
- 2026-11-04 第二轮把审计中的低风险项落地：typed EQ/NEQ + BOOL AND/OR、
  `__edge_unique` 原生 kernel、`CorrelatedSource` chunk-row view、join/apply 结果列
  `copyValueFrom`、DISTINCT typed digest、PE 属性 typed 写入。
- 同二进制 A/B（40 轮，`opt/base` min）：complex-5 **0.957**、complex-10 **0.961**、
  complex-7 **0.966**、complex-14 0.993、complex-1 1.027、complex-12 1.011；其余在
  ±8% 噪声内。稳定收益 3–4%，不是数量级。
- 仍未做：Sort/Materialize/HashJoin 的整行 `Row` 物化（HashJoin 仅输出列 typed copy）、
  typed GROUP BY key、per-group batch aggregate、短查询固定开销。这些需要独立的
  列式缓冲/group map 设计，不能靠局部补丁。
- 本轮未重跑全量 ctest/TCK；`query_executor_tests` 614/614、
  `evaluator_typed_tests` 34/34、`data_chunk_tests` 20/20 已重跑。
