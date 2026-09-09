# LDBC SNB Interactive SF0.1 复测（当前版本）

> 分支：`feature/correlated-apply`（单 commit `bf25c759`）
> 数据目录：`/home/dodo/code/fuck/eugraph-sf0.1-cli-data`（converted Neo4j-header CSV，`Comment:Message` / `Post:Message`）
> 节点 327,588；边 1,477,965
> 查询集：`ldbc_snb_interactive_v1_impls/cypher/queries/` 的 **LDBC 原版查询文本**
> 方法：Bolt 7688，每个查询 1 次预热 + 3 次计时取中位数；单次超时 45s
> 日期：2026-09-09

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
| complex-2 | 20 | 25.51 | 17.98 | 1.42 | 🟢 |
| complex-3 | 0 | 2024.30 | 48.76 | 41.52 | 🔴 |
| complex-4 | 10 | 18.23 | 8.92 | 2.04 | 🟡 |
| complex-8 | 20 | 5.92 | 5.28 | 1.12 | 🟢 |
| complex-11 | 10 | 13.28 | 5.85 | 2.27 | 🟡 |
| complex-12 | 2 | 13.09 | 83.79 | 0.16 | 🟢 |
| short-1 | 1 | 1.06 | 2.82 | 0.38 | 🟢 |
| short-2 | 10 | 5.78 | 5.25 | 1.10 | 🟢 |
| short-3 | 3 | 1.04 | 3.92 | 0.27 | 🟢 |
| short-4 Post | 1 | 0.90 | 2.49 | 0.36 | 🟢 |
| short-4 Comment | 1 | 0.98 | 2.49 | 0.39 | 🟢 |
| short-5 Post | 1 | 0.78 | 2.42 | 0.32 | 🟢 |
| short-5 Comment | 1 | 0.92 | 2.23 | 0.41 | 🟢 |
| short-6 Post | 1 | 1.28 | 2.54 | 0.50 | 🟢 |
| short-6 Comment | 1 | 1.21 | 4.85 | 0.25 | 🟢 |
| short-7 无回复 | 0 | 1.40 | 2.70 | 0.52 | 🟢 |

说明：
- complex-3 用该参数返回 0 行，但查询正常执行完成；正确性未在本轮重新核对。
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

## 4. 当前版本关键优化（`bf25c759`）

1. **Q12 计划重写**：`Apply(collect(tag.id), HashJoin(friend))`，左支从 `Tag(id) IN tags` 反向展开，右支从 `Person(id)` 经 KNOWS 展开。
2. **Expand 批处理**：dst label 一次批量检查；新增 `scanEdgesBatch` 快速路径。
3. **VarLenExpand 邻接缓存**：同一 chunk 内复用每个 vertex 的邻接边和 label 判定，避免多起点重复扫描同一张图。
4. **VarLenExpand OR 索引剪枝**：`WHERE src.prop = v OR dst.prop = v` 下推为 src/dst 索引允许集，Q12 左分支起点从 71 个 TagClass 降为 1 个。

Q12 同机中位数：约 **13ms**（优化前约 1.2s）。

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
| complex-3 | ~41.5x | 多 Expand + LeftJoin + varlen，无有效 join order / 边索引 | join order 调优；VLE 剪枝；LeftJoin 右支延迟物化 |
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

### 5.5 Q12 优化路径记录（保留方法）

1. 计划重写：`Filter(CrossProduct)` → `Apply(collect(tag.id), HashJoin(friend))`；
2. 右支：`IndexScanValues(Tag id IN tags) → 反向 Expand`，补 dst label 约束；
3. 左支：TagClass 起点反向 VLE；同一 chunk 缓存 vertex 邻接边，edge scan 65,689 → 16,151；
4. 左支：OR 下推为 VLE src/dst 索引允许集，71 个 TagClass 起点 → 1 个。
