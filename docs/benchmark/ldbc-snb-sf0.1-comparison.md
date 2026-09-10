# LDBC SNB Interactive SF0.1 复测（当前版本）

> 分支：`perf/ldbc-complex-3`（Q12 优化 `4d6f627` + 本轮 Complex-3 优化）
> 数据目录：`eugraph/build/ldbc-bench/eugraph`（converted Neo4j-header CSV，`Comment:Message` / `Post:Message`）
> 节点 327,588；边 1,477,965
> 查询集：`ldbc_snb_interactive_v1_impls/cypher/queries/` 的 **LDBC 原版查询文本**
> 方法：Bolt 7688，每个查询 1 次预热 + 3 次计时取中位数；单次超时 45s
> 日期：2026-09-10

## 1. 测试环境

| 项 | 配置 |
|---|---|
| server | `eugraph-server --bolt-port 7688 --port 9090` |
| WiredTiger | cache 2048MB，`--wt-txn-sync none`，threads 4 |
| 索引 | loader 自带各标签 `id` 唯一索引 + `Tag(name)` / `TagClass(name)` / `Person(firstName)` / `Message(creationDate)` 等 |

## 2. 已执行查询结果（同机对比）

> 时间均为 1 次预热 + 3 次计时取中位数，单位 ms。
> 图标：🟢 = EuGraph 与 Neo4j 性能相当或更快（倍数 ≤ 1.5）；🟡 = 慢 1.5~5 倍；🔴 = 慢 5 倍以上。
> 倍数 = EuGraph / Neo4j，<1 表示 EuGraph 更快。

| Query | 行数 | EuGraph(ms) | Neo4j(ms) | 倍数 | 图标 |
|-------|-----:|------------:|----------:|-----:|:---:|
| complex-12 | 2 | 13.09 | 83.79 | 0.16 | 🟢 |
| short-6 Comment | 1 | 1.21 | 4.85 | 0.25 | 🟢 |
| short-3 | 3 | 1.04 | 3.92 | 0.27 | 🟢 |
| short-5 Post | 1 | 0.78 | 2.42 | 0.32 | 🟢 |
| short-4 Post | 1 | 0.90 | 2.49 | 0.36 | 🟢 |
| short-1 | 1 | 1.06 | 2.82 | 0.38 | 🟢 |
| short-4 Comment | 1 | 0.98 | 2.49 | 0.39 | 🟢 |
| short-5 Comment | 1 | 0.92 | 2.23 | 0.41 | 🟢 |
| short-6 Post | 1 | 1.28 | 2.54 | 0.50 | 🟢 |
| short-7 无回复 | 0 | 1.40 | 2.70 | 0.52 | 🟢 |
| short-2 | 10 | 5.78 | 5.25 | 1.10 | 🟢 |
| complex-8 | 20 | 5.92 | 5.28 | 1.12 | 🟢 |
| complex-2 | 20 | 25.51 | 17.98 | 1.42 | 🟢 |
| complex-4 | 10 | 18.23 | 8.92 | 2.04 | 🟡 |
| complex-11 | 10 | 13.28 | 5.85 | 2.27 | 🟡 |
| complex-3 | 0 | 101.20 | 48.76 | 2.08 | 🟡 |

说明：
- complex-3 本轮从 2024.30ms 降至约 101ms（同参数、同机）；该参数返回 0 行，但查询正常执行完成。
- complex-3 结果正确性用返回 1 行的 `Sweden/Kazakhstan` 参数与 Neo4j、原计划交叉核对一致。
- `Message(creationDate)` 索引必须是完整索引；本机旧索引只有 1024 条，重建后为 286,744 条（见 5.6）。
- short-7 使用“无回复”的 Comment，避免已知的 OPTIONAL MATCH 超时路径。
- 参数口径见下表。

### 2.1 参数

| Query | 参数 |
|-------|------|
| complex-2 | personId=933, maxDate=1287187200000 |
| complex-3 | personId=933, countryXName=Germany, countryYName=Brazil, startDate=1275350400000, endDate=1277769600000 |
| complex-4 | personId=933, startDate=1262808638903, endDate=1347527502711 |
| complex-8 | personId=933 |
| complex-11 | personId=933, countryName=India, workFromYear=2011 |
| complex-12 | personId=933, tagClassName=Actor |
| short-1/2/3 | personId=933 |
| short-4/5/6 Post | messageId=3 |
| short-4/5/6 Comment | messageId=618475290625 |
| short-7 | messageId=618475290625（无回复） |

## 3. 未执行的查询

| Query | 原因 |
|-------|------|
| complex-1 / complex-13 | 依赖 `shortestPath`，当前不支持 |
| complex-5 / complex-6 / complex-7 | 已知会导致 CPU/内存失控，跳过 |
| complex-9 | 已知超时（>40s），跳过 |
| complex-10 | pattern comprehension 语法不支持 |
| complex-14 | `allShortestPaths` + `reduce` 语法不支持 |
| short-7 有回复版 | 已知 OPTIONAL MATCH 路径超过 240s，跳过 |

## 4. 当前版本关键优化（Q12 `bf25c759` + 本轮 Complex-3）

1. **Q12 计划重写**：`Apply(collect(tag.id), HashJoin(friend))`，左支从 `Tag(id) IN tags` 反向展开，右支从 `Person(id)` 经 KNOWS 展开。
2. **Expand 批处理**：dst label 一次批量检查；新增 `scanEdgesBatch` 快速路径。
3. **VarLenExpand 邻接缓存**：同一 chunk 内复用每个 vertex 的邻接边和 label 判定，避免多起点重复扫描同一张图。
4. **VarLenExpand OR 索引剪枝**：`WHERE src.prop = v OR dst.prop = v` 下推为 src/dst 索引允许集，Q12 左分支起点从 71 个 TagClass 降为 1 个。
5. **Complex-3 索引优先 join**：将 `Expand(friend→message) → Filter(message.creationDate) → Expand(message→country)` 改写为 `HashJoin(candidate friends, IndexScan(Message.creationDate range) → HAS_CREATOR → creator)`；日期谓词在 Expand 之上先拆分为独立 Filter 后再下推。

Q12 同机中位数：约 **13ms**（优化前约 1.2s）。

Complex-3 同机中位数：约 **101ms**（优化前约 2024ms）。

## 5. 历史结论与遗留优化方案（保留）

> 本节只保留仍然有效的根因分析和后续方案；已过时的“历史耗时记录”不再保留。

### 5.1 已知语法能力缺口

| 语句 | 缺口 | 方案 |
|------|------|------|
| complex-1 | `shortestPath` 不支持 | 实现双向 BFS shortest-path 算子 |
| complex-10 | pattern comprehension 不支持 | 支持 pattern comprehension 或等价改写 |
| complex-13 | `shortestPath` 不支持，当前用 `knows*1..3 + min(length(path))` 近似 | 同上 |
| complex-14 | `allShortestPaths` + `reduce` 不支持 | 实现 all-shortest-paths + reduce |

### 5.2 仍然较慢的语句与优化方向

| 语句 | 当前差距 | 已定位原因 | 后续方案 |
|------|---------|-----------|---------|
| complex-3 | ~2.1x | 已改为 Message(creationDate) 索引优先 HashJoin(creator=friend)；剩余在 `KNOWS*1..2` VLE、聚合与顶点物化 | 继续优化 VLE、聚合/排序批量化 |
| complex-4 | ~2.0x | 已修多 pattern 关联；剩余在聚合/排序/ProjectionExtract | 聚合与排序批量化；ProjectionExtract 向量化 |
| complex-11 | ~2.3x | 同上 | 同上 |

### 5.3 仍需避免执行的语句

| 语句 | 现象 | 已定位方向 |
|------|------|-----------|
| complex-5 | CPU/内存失控 | 需先隔离复现，再做 bounded VLE / 去重 |
| complex-6 | 同上 | 同上 |
| complex-7 | OPTIONAL MATCH 组合后失控 | 核心聚合很快；需拆解 OPTIONAL MATCH 关联 |
| complex-9 | 超时 >40s | 多起点消息展开，需 join order / 索引 |
| short-7 有回复版 | 超时 >240s | `OPTIONAL MATCH ...-[r:KNOWS]-(p)` 无方向模式，需边/模式优化 |

### 5.4 已经修复并验证有效的问题（保留结论）

| 问题 | 修复 | 结果 |
|------|------|------|
| 单 MATCH 多 pattern / WITH 后 MATCH 退化为 AllNodeScan + CrossProduct | binder 复用已绑定列续接 Expand/VarLenExpand | complex-4、short-2 不再 AllNodeScan |
| `:Message` 无主标签导致点查走 AllNodeScan | loader 多标签 + weak index + `Filter(LabelScan)->IndexScan` 触发 | short-4/5/6 降至 ~1ms |
| Q12 `tag.id IN tags` 未下推、前向链 1.2s | Apply + HashJoin(friend) + Tag 反向链 + VLE 邻接缓存 + VLE OR 索引剪枝 | Q12 降至 ~13ms |
| complex-3 先按 friend 展开 45,723 条 message，再做 creationDate 过滤 | WHERE 顶层 AND 拆分 + 谓词下推 + `IndexScan(Message.creationDate) → HAS_CREATOR → HashJoin(friend)` | 2024ms → ~101ms |

### 5.5 Q12 优化路径记录（保留方法）

1. 计划重写：`Filter(CrossProduct)` → `Apply(collect(tag.id), HashJoin(friend))`；
2. 右支：`IndexScanValues(Tag id IN tags) → 反向 Expand`，补 dst label 约束；
3. 左支：TagClass 起点反向 VLE；同一 chunk 缓存 vertex 邻接边，edge scan 65,689 → 16,151；
4. 左支：OR 下推为 VLE src/dst 索引允许集，71 个 TagClass 起点 → 1 个。

### 5.6 Complex-3 优化路径记录（本轮）

1. **根因**：最后一条 MATCH 的 `WHERE message.creationDate >= start AND < end AND country IN [...]` 被绑定为一个 Filter，计划先把每个候选 friend 的全部 message 展开出来（该参数为 45,723 条），再过滤 creationDate。
2. **AND 拆分**：binder 在 WHERE 直接作用于 Expand/VarLenExpand 时，把顶层 AND 拆成嵌套 Filter；这样 date 谓词可以下推到 `Expand(message→country)` 之前，而 `country IN [...]` 留在 Expand 之后。拆分前先递归检查 Expand，避免破坏 `Filter(LabelScan) → IndexScan` 的复合索引匹配。
3. **索引优先 join**：物理计划将 `Filter(date) → Expand(friend→message, IN HAS_CREATOR)` 重写为 `HashJoin(left=candidate friends, right=IndexScan(Message.creationDate range) → Expand(message→creator, OUT HAS_CREATOR))`，把 45,723 次“逐 friend 展开消息”降为日期索引命中的 3,389 条消息。
4. **索引 range 分页修复**：`AsyncGraphDataStore` 的索引 range 扫描原先每轮都会从起点重扫第一批（结果 >1024 条时重复）；改为先收集命中集、再按 1024 分块 yield，保证 3,389 条消息全部返回。
5. **跨类型 join 等值**：HashJoin 现在把 `VertexValue` / `VertexRef`（以及 `EdgeValue` / `EdgeKey`）按同一个 id 视为相等，使左支构造出的 friend 顶点能与右支拓扑引用 creator 正确 join。
6. **依赖**：`Message(creationDate)` 必须是完整索引。本机旧索引只有 1024 条，重建后为 286,744 条；未修复索引时该改写不会启用（也没有结果错误，只是退回原计划）。
