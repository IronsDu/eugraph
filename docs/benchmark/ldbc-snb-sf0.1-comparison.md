# LDBC SNB Interactive SF0.1 复测（当前版本）

> 分支：`feat/pattern-expression-in-return`（main `c63e3bd` + 本轮 RETURN/ORDER BY pattern expression 支持）
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
| short-7 有回复 | 19 | 7.02 | 9.27 | 0.76 | 🟢 |
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
| complex-5 / complex-6 | 已知会导致 CPU/内存失控，跳过 |
| complex-9 | 已知超时（>40s），跳过 |
| complex-10 | pattern comprehension 语法不支持 |
| complex-14 | `allShortestPaths` + `reduce` 语法不支持 |

## 4. 当前版本关键优化（Q12 + Complex-3 / Short-7 + Complex-7 原版支持）

1. **Q12 计划重写**：`Apply(collect(tag.id), HashJoin(friend))`，左支从 `Tag(id) IN tags` 反向展开，右支从 `Person(id)` 经 KNOWS 展开。
2. **Expand 批处理**：dst label 一次批量检查；新增 `scanEdgesBatch` 快速路径。
3. **VarLenExpand 邻接缓存**：同一 chunk 内复用每个 vertex 的邻接边和 label 判定，避免多起点重复扫描同一张图。
4. **VarLenExpand OR 索引剪枝**：`WHERE src.prop = v OR dst.prop = v` 下推为 src/dst 索引允许集，Q12 左分支起点从 71 个 TagClass 降为 1 个。
5. **Complex-3 索引优先 join**：将 `Expand(friend→message) → Filter(message.creationDate) → Expand(message→country)` 改写为 `HashJoin(candidate friends, IndexScan(Message.creationDate range) → HAS_CREATOR → creator)`；日期谓词在 Expand 之上先拆分为独立 Filter 后再下推。
6. **Short-7 OPTIONAL MATCH 相关性链修复**：起点已绑定时不再因为终点也已绑定就退化为独立扫描 + CrossProduct，而是继续用 `CorrelatedSource → Expand(start→new) → Expand(new→bound_endpoint)`；同时修复了 complex-7 的 OPTIONAL MATCH 等价改写。
7. **Complex-7 原版 pattern expression 支持**：`NOT`、`AND` / `OR`、`CASE WHEN`、`ANY` / 列表推导 WHERE、`EXISTS { pattern }` 等布尔上下文中的 bare pattern 解析为 `ExistsExpr`，经 `BoundPatternComprehensionApplyOp` hoist 后绑定为 `size(list) > 0`；原版 complex-7 已可执行，结果与 Neo4j 逐行一致。

Q12 同机中位数：约 **13ms**（优化前约 1.2s）。

Complex-3 同机中位数：约 **101ms**（优化前约 2024ms）。

Short-7 有回复中位数：约 **7ms**（优化前超时 >240s；Neo4j 同机约 9ms）。

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
| complex-7 | 见 §8（修正后基线 3.4x~12.5x） | **不是** pattern expression 主导：主因是中间宽列物化 + 逐行 `Value` variant 拷贝/堆分配；pattern expression 约占 25% | 见 §8.5 |
| complex-4 | ~2.0x | 已修多 pattern 关联；剩余在聚合/排序/ProjectionExtract | 聚合与排序批量化；ProjectionExtract 向量化 |
| complex-11 | ~2.3x | 同上 | 同上 |

### 5.3 仍需避免执行的语句

| 语句 | 现象 | 已定位方向 |
|------|------|-----------|
| complex-5 | CPU/内存失控 | 需先隔离复现，再做 bounded VLE / 去重 |
| complex-6 | 同上 | 同上 |
| complex-9 | 超时 >40s | 多起点消息展开，需 join order / 索引 |

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

## 6. 执行计划对比与扩展性分析（2026-09-10）

> 目的：记录 complex-3 / q12 优化后与 Neo4j 的计划差异，以及数据规模增长时当前启发式计划的风险。

### 6.1 complex-3：EuGraph 与 Neo4j 已采用不同 join 策略

参数：`personId=933, Germany/Brazil, start=1275350400000, end=1277769600000`。

#### Neo4j 计划（简化，自底向上）

```text
NodeUniqueIndexSeek(Person id)
  + NodeIndexSeek(Country name) × 2
  -> CartesianProduct -> Limit 1
  -> Apply(NodeByLabelScan(Country)
           -> Expand(Country) <-[IS_PART_OF]- (City)
           -> Filter City)
  -> EagerAggregation(collect(City) AS cities)
  -> VarLengthExpand(Pruning, BFS, All) (person)-[:KNOWS*1..2]-(friend)
  -> Filter NOT person = friend
  -> Expand(All) (friend)-[IS_LOCATED_IN]->(City)
  -> Filter NOT City IN cities
  -> Distinct friend
  -> Expand(All) (friend)<-[HAS_CREATOR]-(message)
  -> Filter message.creationDate
  -> Expand(All) (message)-[IS_LOCATED_IN]->(Country)
  -> Filter Country IN [countryX, countryY]
  -> EagerAggregation(sum) -> Top 20
```

#### EuGraph 计划（简化，自底向上）

```text
IndexScan(Country name) × 2 + IndexScan(Person id)
  -> CrossProduct -> Limit 1
  -> CrossProduct with (
       LabelScan(City)
       -> Expand(City)-[IS_PART_OF]->(Country)
       -> Filter Country IN [countryX, countryY]
     )
  -> Aggregate collect(City)
  -> VarLenExpand(person)-[:KNOWS*1..2]-(friend)
  -> Filter NOT person = friend
  -> Expand(friend)-[IS_LOCATED_IN]->(City)
  -> Filter NOT City IN cities
  -> Project -> Distinct friend
  -> HashJoin(
       left  = 上述 friend 行,
       right = IndexScan(Message.creationDate range)
               -> Expand(message)-[HAS_CREATOR]->(creator)
     )
  -> Expand(message)-[IS_LOCATED_IN]->(Country)
  -> Filter Country IN [countryX, countryY]
  -> Aggregate sum -> Sort -> Limit 20
```

#### 关键差异

| 部分 | Neo4j | EuGraph 当前 | 影响 |
|---|---|---|---|
| City 获取 | 从两个具体 Country 反向 Expand 到 City，走 Apply | 扫全部 City，再 Expand 到 Country 后过滤 | City 总量小（1,007），当前影响有限，但 SF 放大后值得改 |
| `KNOWS*1..2` | `VarLengthExpand(Pruning, BFS, All)` | `VarLenExpand(1..2, ANY)`，无 BFS 剪枝 | 候选 friend 生成的主要成本差异之一 |
| message 侧驱动 | 逐 friend 展开其全部 message 后再过滤日期 | 先扫日期窗口内的全局 message，再 HashJoin 回 candidate friend | 数据分布相关性完全不同 |
| message 展开量（SF0.1 本参数） | 45,723（候选 friend 的全部 message） | 3,389（日期窗口全局 message） | 当前参数下索引优先少约 13.5 倍展开量 |
| 日期过滤后行数 | 752 | 752 | 最终语义一致 |
| join 实现 | 顺序 Expand + Filter | 自定义 `HashJoin` | 后者多一次 hash build/probe 和 Value 物化 |

### 6.2 q12：计划结构基本同构

q12 两边都是：

```text
Apply(
  left  = collect(tag.id) AS tags 的 TagClass / Tag 链,
  right = HashJoin/NodeHashJoin(friend)(
            Person(id) -> KNOWS -> friend,
            tag.id IN tags -> HAS_TAG / REPLY_OF / HAS_CREATOR -> friend
          )
)
```

差异：

| 部分 | Neo4j | EuGraph |
|---|---|---|
| friend join | `NodeHashJoin(friend)` | 自定义 `HashJoin(friend)` |
| tag 探测 | `NodeUniqueIndexSeek(tag.id IN tags)` | `IndexScanValues(tag.id IN tags)` |
| VLE | `VarLengthExpand(All)` | `VarLenExpand` |
| 额外算子 | CacheProperties / 原生算子 | 更多 `ProjectionExtract` |

因此 q12 的剩余差距主要来自算子实现常数，而不是 join 顺序；随 SF 增长，双方输入应该大致同比例放大，扩展性风险低于 complex-3。

### 6.3 数据规模增长时的风险模型

两种 complex-3 计划的 message 段成本可近似为：

```text
cost_friend ≈ E_friend × c_expand + E_friend_date × c_country
cost_index  ≈ E_window × c_index + E_matched × c_hash_join + E_matched × c_country
```

其中：

- `E_friend`：候选 friend 的全部 message 数；
- `E_window`：固定日期窗口内的全局 message 数；
- `E_friend_date` / `E_matched`：日期过滤后仍需做 country Expand 的行数。

在 LDBC 这类图中，候选 friend 数、人均 message 数随 SF 增长通常远慢于消息总量；而固定时间窗口的全局 message 数约随总 message 数线性增长。

因此：

| 规模 | 假设 E_friend 近似不变（≈45,723） | E_window 近似 | 索引优先是否仍占优 |
|---|---:|---:|---|
| SF0.1 | 45,723 | 3,389 | 是（约 13.5x） |
| ×10 / SF1 附近 | 45,723 | 约 33,890 | 是，但仅约 1.35x |
| ×30 / SF3 附近 | 45,723 | 约 101,670 | 否，friend 驱动更优 |
| ×100 / SF10 附近 | 45,723 | 约 338,900 | 否，索引优先明显更差 |

> 表中 SF 倍数为按 SF0.1 消息总量线性外推的粗略数量级，实际 crossover 取决于候选 friend 数、日期分布、人均 message 数和数据规模下 VLE 集合的增长。

结论：

1. complex-3 当前索引优先改写是**选择性驱动**的：日期窗口越窄、全局窗口 message 越少，收益越大；
2. 数据规模增大后，当前启发式可能从领先变为落后，**不能假定性能倍数固定为 2x**；
3. q12 的 join 顺序与 Neo4j 同构，风险较低，剩余差距主要是算子常数。

### 6.4 根因：不是“缺一个叫 CBO 的模块”，而是缺少代价化选择

EuGraph 已有 `LogicalOptimizer` / `Memo` / `LogProp` / `CostModel` 骨架，但当前 complex-3 的索引优先计划和 q12 的部分计划重写，是在物理计划阶段做**形状匹配后的启发式改写**：

- complex-3：`PhysicalPlanner::tryPlanFilterDestinationIndexJoin()` 命中形状且索引存在就直接改写，不与 friend 驱动计划做代价比较；
- q12：`PhysicalPlanner::tryPlanListIndexJoin()` 同样是启发式改写。

当前自适应能力不足的原因包括：

1. **搜索空间缺少候选**：Memo 中没有“日期索引优先 + HashJoin”这个逻辑等价候选，代价模型没有机会选择它或拒绝它；
2. **统计信息不足**：缺少 `Message(creationDate)` 的 min/max、分位数、区间行数，以及候选 friend 集合的基数估计；
3. **选择性估计不足**：`LogProp` 对 Filter/Expand 主要依赖平均度数和默认选择性，无法区分窄日期窗口与全历史窗口；
4. **代价模型不足**：当前 local cost 对 IndexScan 区间 I/O、HashJoin build/probe、Value 物化、聚合/Sort 批量化建模不足。

此外，Neo4j 已有的 EXPLAIN 估算在该数据上也并不总是准确；这说明要获得稳定的自适应计划，必须先有数据分布相关的统计，而不是只增加规则数量。

### 6.5 后续优化路线

1. **短期：给 complex-3 改写加代价 guard**
   估算 `E_window` 与 `E_friend`，仅当日期窗口足够选择性时才启用索引优先计划，否则回退到“日期 Filter 已下推的 friend 驱动计划”。这是当前投入产出比最高的一步。

2. **中期：把两种 join order 纳入 CBO**
   增加 `creationDate` 区间统计/直方图，改进 `LogProp` 的 range selectivity，并在 Memo 中生成两个候选计划，由 `CostModel` 选择。

3. **长期：降低算子单行常数**
   即使计划选择正确，`Expand`、`HashJoin`、`Aggregate`、`ProjectionExtract` 的批量化和物化开销仍需要继续优化；q12 是典型例子。

4. **验证：按 SF 逐级复测**
   至少覆盖 SF0.1 / SF1，观察 complex-3 的 ratio 是否随 `E_window/E_friend` 变化，验证代价 guard 和 CBO 选择是否符合预期。

## 7. SHORT-7 / COMPLEX-7 的 OPTIONAL MATCH 相关性优化（2026-09-10）

### 7.1 根因

`bindOptionalMatch` 之前有一个分支条件：

```text
first_node_bound && all_bound_nodes && bound_vars.size() > 1
  -> 把该 OPTIONAL MATCH 的 pattern 独立绑定，
     再通过 CrossProduct + 等值 Filter 与 CorrelationSource 拼接
```

对于 SHORT-7 这类形状：

```cypher
MATCH (m:Message {id: $messageId})<-[:REPLY_OF]-(c:Comment)-[:HAS_CREATOR]->(p:Person)
OPTIONAL MATCH (m)-[:HAS_CREATOR]->(a:Person)-[r:KNOWS]-(p)
```

`m` 和 `p` 都已绑定，因此走了独立绑定分支，右支计划退化为：

```text
CrossProduct
  CorrelatedSource(m, p)
  AllNodeScan(m)
    -> Expand(m → a)
    -> Expand(a → p)
```

即在右支重新全图扫描一次 `m`，再和左侧做 CrossProduct。

### 7.2 修复

只要 pattern 的起始节点已绑定，就继续从该节点做相关性展开，即使另一个端点也已绑定：

```text
CorrelatedSource(m, p)
  -> Expand(m)-[:HAS_CREATOR]->(a)
  -> Expand(a)-[r:KNOWS]-(p)   // p 作为 bound destination 检查
```

绑定的终点由物理 `Expand` 的 `dst_bound` 路径处理，不再需要重新扫描起点。

### 7.3 验证

- TCK `Match7.feature`：31/31 通过。
- 所有包含 `OPTIONAL MATCH` 的 TCK feature：275/275 通过。
- `query_executor_tests`：511/511 通过（新增 pattern expression 回归用例）。
- SHORT-7 有回复（`messageId=893353237791`）：
  - 修复前：超时 >240s；
  - 修复后：19 行，同机中位数约 7.02ms；
  - Neo4j 同参数：19 行，中位数约 9.27ms；
  - 两边返回结果逐行一致。

### 7.4 COMPLEX-7 原版支持

原版 complex-7 最后返回项包含 pattern expression：

```cypher
not((liker)-[:KNOWS]-(person)) AS isNew
```

本轮实现了**布尔上下文**中的 bare pattern expression：

- parser：投影项、ORDER BY、WITH、函数参数与 `CASE THEN` 仍拒绝 bare pattern（与 Neo4j 一致）；`NOT`、`AND` / `OR` / `XOR`、`CASE WHEN`、`ANY` / `ALL` / `NONE` / `SINGLE` 的 WHERE、列表推导 WHERE 等布尔上下文允许 bare pattern；
- binder：`collectPatternComprehensionsAST` 收集 ExistsExpr，复用 `existsToPatternComprehension` 生成合成 PatternComprehension，并经 `BoundPatternComprehensionApplyOp` hoist；
- `bindExpression` 绑定 ExistsExpr 时直接生成 `size(list) > 0`，因此外层的 `NOT` / `AND` 等算子在类型检查前就拿到 boolean，而不是先看到 list 占位符；
- `patchPatternComprehensionPlaceholders` 保留为普通 pattern comprehension 和其他占位符路径的兜底。

验证：

- `personId=1242`：EuGraph 原版 complex-7 返回 20 行，与 Neo4j 原版逐行一致；
- 回归：`query_executor_tests` **519/519** 通过；`cypher_parser_tests` 81/81 通过。新增用例覆盖：
  - `NotBarePatternExpressionInReturn`
  - `BarePatternInBooleanOperators`
  - `BarePatternInCaseWhen`
  - `BarePatternInAnyWherePredicate`
  - `BarePatternInListComprehensionWhere`
  - `ExistsPatternSubqueryInReturn` / `ExistsPatternSubqueryWithWhereInReturn`
  - `BarePatternExpressionDirectInReturnError`、`BarePatternExpressionInOrderByError`、`BarePatternExpressionInWithError`、`BarePatternExpressionInFunctionArgError`、`BarePatternExpressionInCaseThenError`

已支持的 pattern expression 形式（布尔上下文）：

- `RETURN not((n)-[:KNOWS]-(m)) AS x`
- `RETURN ((n)-[:KNOWS]-(m)) AND true AS x` / `OR false`
- `RETURN CASE WHEN (n)-[:KNOWS]-(m) THEN 1 ELSE 0 END`
- `WHERE (n)-[:KNOWS]-(m)`
- `RETURN EXISTS { (n)-[:KNOWS]->(m:Person) WHERE m.name = 'name2' } AS x`
- `ANY(x IN [1, 2] WHERE (n)-[:KNOWS]->(:Person))`
- `[x IN [1, 2] WHERE (n)-[:KNOWS]->(:Person)]`

与 Neo4j 一致地拒绝非布尔直接使用：

- `RETURN (n)-[:KNOWS]->(:Person) AS x`
- `ORDER BY (n)-[:KNOWS]->(:Person)`
- `WITH (n)-[:KNOWS]->(:Person) AS x`
- `coalesce((n)-[:KNOWS]->(:Person), false)`
- `CASE WHEN true THEN (n)-[:KNOWS]->(:Person) ELSE false END`

`EXISTS { MATCH ... }` 完整子查询仍不支持。

## 8. COMPLEX-7 基线与优化（2026-09-10，修正 §7.4 的对比口径）

### 8.1 根因：§7.4 的 Neo4j 基线不可比

§7.4 曾记录「personId=1242：EuGraph 约 58ms / Neo4j 约 6ms」。该对比有两处数据不对称，**Neo4j 侧实际未做等价工作**：

| 不对称点 | Neo4j 侧事实 | EuGraph 侧事实 | 后果 |
|---|---|---|---|
| `:Message` 标签 | **不存在**（只有 `Comment` / `Post`） | 存在（loader 建了 `Comment:Message` / `Post:Message`） | Neo4j 的 `MATCH (message:Message)` 命中 0 个节点，计划为 `NodeByLabelScan(message:Message)` 空扫 |
| `creationDate` 类型 | **STRING**（`neo4j-local/convert_for_neo4j.py` 声明 `:LONG` 但未生效） | INT64（loader 采样推断，`csv_loader.cpp`） | 原版 LDBC 文本的 `likeTime - msg.creationDate` 在 Neo4j 上抛 `CypherTypeError: Cannot subtract String from String` |

因此「Neo4j 6ms」是**空扫 0 行**的耗时；personId=1242 实际有 **416 条 like 边**。

### 8.2 修正后的等价值基线

同一份 sf0.1 数据、等价值查询（`WHERE (message:Comment OR message:Post)`；Neo4j 侧日期加 `toInteger`），两边返回行与首行字段逐字段一致。基准脚本：[`scripts/bench_ldbc_complex7.py`](../../scripts/bench_ldbc_complex7.py)：

```bash
python3 scripts/bench_ldbc_complex7.py --both --variant eqv --person-id 933,1242,2199023256816 --iters 20
```

> 注意事项：本机为共享开发机，**绝对耗时会随负载漂移**（同一版本不同时刻可差 10~20%）。脚本同时输出 `min` / `p25` / `median`，其中 `min` 是最少受其他负载干扰的一轮，建议以 `min` 与**倍数**为准，不要跨时刻直接比较绝对值。

| personId | like 边数 | EuGraph min | Neo4j min | 倍数（min） | 行数 |
|---|---:|---:|---:|---:|---:|
| 933 | 45 | 7.5ms | 1.6ms | 4.6x | 7 |
| 1242 | 416 | 34.2ms | 2.6ms | 13.1x | 20 |
| 2199023256816 | 1944 | 123.7ms | 5.3ms | 23.4x | 20 |

> 上表为 §8.4 + §8.5 两项优化**均已生效**后的数值；Neo4j 侧同一轮次测量。
>
> ⚠️ **绝对值与倍数都随机器负载漂移**：同一份代码，机器较空闲时 EuGraph/Neo4j 分别读到 123.7/5.3ms（23.4x），机器较忙时读到 150.1/7.4ms（20.3x）。两者并非等比例变化——Neo4j 对 CPU 争用更敏感，因此**总负载越高，倍数看起来越小**。跨时刻比较绝对值或倍数都不可靠；判断某次优化是否有效，必须用**同一二进制、背靠背交替测量**（见 §8.4 / §8.5 的做法）。

关键特征：**EuGraph 耗时随 liker 数线性劣化，Neo4j 基本平坦** —— 属于系统性逐行开销，而非 join 顺序选错。以 SF0.1 的 like 边数换算，EuGraph 约 0.08 ms/边，Neo4j 约 0.004 ms/边（且 Neo4j 侧还包含 `toInteger` 字符串转换开销）。

### 8.3 耗时归因（修正 §5.2）

按流水线阶段实测（pid=2199023256816 / 1242）：

| 阶段 | 2199023256816 | 1242 |
|---|---:|---:|
| `MATCH` 三段 Expand（~1944 行） | 31.9ms | — |
| `+ WITH liker, message, likeTime, person` | 83.7ms（+51.8） | — |
| `+ ORDER BY likeTime, toInteger(message.id)` | 99.2ms（+15.5） | — |
| `+ head(collect({...}))` | 128.1ms（+28.9） | 37.8ms |
| `+ pattern expression` | +46ms | +12ms |
| `+ 末尾 RETURN / ORDER BY / LIMIT 20` | 142.8ms | 51.3ms |

结论：**pattern expression 只占约 25%**（§5.2 原先把 complex-7 的差距整体归因于它，是错的）。主因是中间宽列的逐行物化。

perf（RelWithDebInfo + 循环执行，13042 样本）自耗时分布：

| 类别 | 占比 |
|---|---:|
| 堆分配器（`operator new` / `malloc` / `free`） | **17.2%** |
| `std::variant` 派发 | **14.1%** |
| `VertexValue` 拷贝构造 | 5.2% |
| WiredTiger 全部 `__wt_*` 合计 | ~5% |

即 CPU 几乎全部消耗在行级 `Value` 装箱/卸载上，存储只占 ~5%。

进一步下钻（同一份 profile，去掉采样自身开销后）最大的一簇是 **`VertexValue` 的完整物化**：`VertexValue::VertexValue` 拷贝构造 + 其内部 `unordered_map<LabelId, Properties>` 的节点分配，合计约 **6%**，仅次于分配器总和。来源是 complex-7 的 `collect({msg: message, likeTime: likeTime})` —— `message` 作为**整对象**进入 map，于是每个 (message, like) 组合都要构造一个带全部属性容器的 `VertexValue`，而最终 `head(...)` 每组只保留一个。这与 §8.6 第一条（whole-object 构造点）是同一根因在不同层面的表现。

### 8.4 已实施的优化：existence-only 提前退出（pattern expression）

`not((liker)-[:KNOWS]-(person))` 及 `EXISTS { ... }`、`WHERE (a)-->(b)` 这类布尔上下文的 pattern predicate，其合成 comprehension 的列表**只被 `size(list) > 0` 消费**，列表内容是不可观测的。改动：

- `Binder`：`collectExistenceDerivedPatterns` 标出由 `ExistsExpr` 派生的 comprehension，置 `BoundPatternComprehensionApplyOp::existence_only`；
- 物理算子：`existence_only` 时在遇到第一个匹配行后**立即停止拉取关联子计划**，并以单元素占位列表输出（`0 > 0` = false / `1 > 0` = true，真值完全一致），从而跳过 `collect()` 全量物化；
- 用户可见的 pattern comprehension（`[(a)-->(b) | b]`）不受影响，`existence_only` 恒为 false。

同条件 A/B（同一二进制、环境开关切换，各 25 次迭代）：

| 参数 | 关闭 | 开启 | 收益 |
|---|---:|---:|---:|
| personId=1242 | min 47.9 / p25 50.7 / med 52.7 | min 41.3 / p25 44.0 / med 45.5 | min **-13.8%** |
| personId=2199023256816 | min 176.6 / p25 182.5 / med 187.0 | min 148.6 / p25 154.4 / med 155.5 | min **-15.9%**，p25 -15.4% |

### 8.5 已实施的优化：Sort 逐行分配收敛

`SortPhysicalOp` 原先为每行分配**两个**独立堆对象：payload 行（`std::vector<Value>`）与并列的排序键行（`std::vector<Value>`）。把排序键直接追加到同一 `Row` 尾部（payload 占 `[0, num_cols)`，键紧随其后），每行只剩一次分配，比较器按偏移直接索引。

改动本身是 O(1) 的行内布局调整，无语义变化。背靠背两轮 A/B（每轮基线/改动各 30 次迭代，第二轮调换测量顺序以排除漂移）：

| 参数 | 轮次 | 基线 min | 改动后 min | 收益 |
|---|---|---:|---:|---:|
| 2199023256816 | 1 | 166.17 | 159.18 | -4.2% |
| 2199023256816 | 2 | 166.36 | 155.48 | -6.5% |
| 1242 | 1 | 47.09 | 45.18 | -4.1% |
| 1242 | 2 | 47.78 | 45.92 | -3.9% |

收益不大但两轮方向一致；保留。

### 8.6 尝试过但**证伪**的方案（保留结论，避免重复踩坑）

| 方案 | 结论 | 证据 |
|---|---|---|
| 让 `WITH n` 纯转发不抬升 whole-object 需求（phase-aware demand） | **不可行，已回滚** | `query_executor_tests` 从 519/519 降到 495/519（24 失败：`WithWhere`、`WithStarPassthrough`、`CreateNode*`、`ExecuteReturnVertex` 等）。根因：whole-object 需求是**承重的** —— `lowerAliasPassthrough` 依据 `PEPlan.object_slot_id` 决定转发列是否提升到 object slot，且约 52 处下游消费者直接 `std::get<VertexValue>`，不认识 `VertexRef`。移除需求会让 `n.age` 这类谓词读到空列 |
| 聚合分组键用引用而非整对象（`isBareGraphRef` 分组键版本） | **不可行，已回滚** | `labels(v)` / `type(r)` 等函数对分组键仍要求整对象，`ANY(x IN ...)` 等列表消费者也要求真实列表内容；破坏面约 52 处 |
| Aggregate 逐 chunk 的 scratch 向量/行向量提升为复用缓冲 | **负优化，已回滚** | 实测重载参数从 min 148.3ms 退化到 224.9ms（+52%）；预分配 1024×n_args 的 `Value` 向量并反复复用，反而增加了 `Value` 拷贝与移动赋值开销 |
| 用 `LD_PRELOAD` malloc 采样器 / 全局 `operator new` 计数做分配归因 | **未能落地** | 协程挂起点把算子帧与叶子分配割裂，`perf --call-graph dwarf` 与 `backtrace()` 都看不到算子帧，无法把分配归因到具体算子；放弃该类归因，改为「最小改动 + 背靠背 A/B」逐个验证 |
| `head(collect(x))` 识别为「保首值」：给 `AggStateBase` 加 `keep_first_only`，collect 每组只留第一个元素（并让 `finalize` 改为 move 而非再深拷贝一遍） | **无净收益，已回滚** | 单二进制 + 环境开关背靠背两轮 A/B：重载参数 117.6/121.0（关闭）vs 116.1/119.5 与 116.6/121.1（开启），轻载参数同样在 ±2% 内、方向不一致。**根因**：整对象物化的开销发生在 collect **上游**的 `ProjectionExtract::ConstructVertex`（批量取 label/属性并构建完整 `VertexValue`），`collect` 状态本身只持有已被物化对象的拷贝，因此限制它并不触及主成本 |
| Sort 输出列按输入 kind 定型（避免每格走 `ANY` 的 `Value` 装箱） | **负优化，已回滚** | 单二进制 + 环境开关背靠背 A/B：`ANY` 输出 min 32.9/118.7，定型输出 min 32.7/119.3 —— 轻载参数持平，重载参数略差（med 121.2 → 124.4）。**根因**：`VertexValue` 这类大对象走定型列需要按 map 存储拷贝，反而比 variant 更贵；而消费方一律经 `getValue` 读取，定型没有带来读侧收益 |
| `setValue` 增加 `Value&&` 重载，让 type-erased 列「搬入」而不是深拷贝（`ConstructVertex` 每行少一次 `VertexValue` 深拷贝） | **无收益，已回滚** | 单二进制 + 环境开关背靠背 A/B：搬入 min 121.0 / med 131.6，搬出（拷贝）min 121.3 / med 127.6 —— 差异落在 ±5% 噪声内。**教训**：该改动首次单独测量得「min 150.1→132.0、med 160.1→140.0」，看似 12~17% 大胜，实为机器负载变化造成的假象。没有同二进制交替对照的「收益」一律不可信（见 §8.2 的负载告警） |

### 8.7 后续方向（按收益排序）

> **结论先行（第 4 轮更新）**：complex-7 剩余差距的来源已被收敛到**计划形状本身**，而不是某个可以局部替换的实现细节。要继续提速需要改「每组只取第一行」的早期剪枝机制（见下），这属于执行器层面的改动。

1. **按组提前剪枝（唯一仍有量级空间的方向）**。已确认的事实链：
   - 剩余最大成本簇是 `ProjectionExtract::ConstructVertex`：对**每一行**拉取该 vertex 的**全部**属性（`batchGetVertexProperties(..., {})`，空 projection 表示不过滤属性）并构建完整 `VertexValue`（含 `unordered_map` 分配），≈6%；
   - complex-7 的 `head(collect({msg: message, ...}))` 每组最终只用**第一个** message，即 1944 行里只有 542 行的 `message` 真正被需要；
   - 但 `message` 必须**在进入 `collect` 的 map 之前**构造完成，因此在 `Aggregate` 处限制状态（8.6 已验证）或把构造推迟到 `Sort`/`Aggregate` 之后都无效。
   
   唯一可行的做法：让 `head(collect(...))` 触发「每组只保留首行」的 hint，并把该 hint **沿 Project → ProjectionExtract → Sort 上游传播**，使非首行的 vertex 构造与属性读取根本不发生（预计可去掉 1944→542 行、约 3.6 倍的物化量）。这需要执行器的 per-group early-exit 机制，属于架构级改动。
   
   > 注：第 4 轮已实测确认「把构造点后移到聚合之后」这一 (A) 的原始表述**不足以**解决问题——因为构造被 `collect` 的 map 语义钉在聚合之前。修正后的目标表述应为上面的「按组剪枝」。

2. **降低行级 `Value` 装箱成本**（分配器 17% + variant 派发 14%）。已验证：单独消除局部拷贝（8.6 `Value&&`、typed Sort 输出）收益为零到负；`VertexValue` 走定型列反而更贵。要动这块必须是成套改动，预期收益也不及第 1 项。

### 8.8 第 5–7 轮的补充实测：为什么「少读属性 / 不构造 VertexValue」省不出时间

**实验（同一 query 骨架，只改 `collect` 的 map 装什么）**

| 变体 | min | p25 | med |
|---|---:|---:|---:|
| C1 `collect({msg: message, ...})` 装整 vertex | 124.22 | 129.00 | 131.81 |
| C2 `collect({msg: {mid: message.id, mcd: message.creationDate, mc: message.content, mif: message.imageFile}, ...})` 只装扁平属性 | 124.35 | 127.67 | 128.73 |

**C2 并不更快。** 且 DPL 已经自动走了「只读所需属性」这条路：C2 的需求为 `var=message whole_vertex=false coalesce=4`，PE 规格为 `__pe_*<vprop-coalesce[2.6,5.4,6.4]>` —— `message` 根本没被构造，只按 label 属性对做 coalesced 扁平加载。也就是说「通过 ProjectionExtract 只读取需要的属性」**就是当前实现**，而它比构造整对象还略慢（多列 flatten 的 setValue 开销 ≥ 一次 map 插入）。

**逐阶段实测（pid=2199023256816）**

| 阶段 | min | 增量 |
|---|---:|---:|
| S1 `MATCH ... RETURN count(*)`（三段 Expand，1944 行） | 22.0 | — |
| S2 `+ WITH liker, message, likeTime, person` | 61.2 | **+39.2** |
| S3 `+ ORDER BY likeTime, createDate` | 71.3 | +10.1 |
| S4 `+ head(collect({map}))` | 97.2 | +25.9 |
| 完整 complex-7 | 124.2 | +27.0 |

**纯 Expand 只占 22ms；S2→S4 的 75ms（占 60%）全是逐行算子开销**（物化宽列 + 排序 + 聚合 map + 收尾投影），约 20µs/行/列。属性读取量在总耗时里不可见 —— 这正是 C1≈C2 的原因。

**结论：杠杆是「行数」，不是「属性读取量」。** 剪枝的收益来自 1944→542 行，而非省掉属性读取。

**构造点为何钉在 Sort 上游（机制已定位）**

1. 触发点：`requirement_collector.cpp:244` —— `Project` 中出现指向图变量的**裸列引用**即置 `need_entire = true`；complex-7 聚合输入的 `Project(items=[liker, message, likeTime, person])` 正是这种裸转发（C2 中 `message` 是属性访问，故不置位）。
2. 构造位置 = Enricher 在计划中的位置：`materializeChosen(VertexEnrich)` 把 `enrich_output` 应用到**其直接子树**，`applyEnrichInPlace` 再一路走到产出该变量的 scan/expand。
3. Enricher 被放在 `message` 的产出组（早于 `Sort`），故 1944 行各构造一个完整 `VertexValue`。
4. `Project` 处刻意不重跑 PE（`physical_planner.cpp:2621` 有注释说明），构造点无法在 Project 处补救。

**因此 (A) 的正确表述**不是「把 Demand-Pull 下推到聚合之后」，而是：**把整对象 Enricher 的插入位置沿计划下移，越过只做行数削减的屏障（`Sort` / `Filter`），但仍留在 `Aggregate` 之前**（聚合改变行形状，Enricher 必须在其之前）。这是语义等价的重排：`Sort` 只重排、`Filter` 只删行。按此规则，`message` 的构造点将由「Expand 之后（1944 行）」移到「Sort 之后（542 行）」。

实现落在 **Enricher 插入位置**（`memo.cpp` 的 enforcer 生成 + `physical_planner.cpp` 的 `materializeChosen` / `applyEnrichInPlace`），**不是** `lowerAliasPassthrough` / 需求语义那一块（8.6 第一条失败的正是后者）。需要处理的细节：整对象需求下移的同时，`Sort` / `Filter` 自身需要的**扁平属性需求必须留在上游**，即需求要按屏障切成两段。**该改动需要开发者评审后再实施**（见 §8.9）。

### 8.9 待评审的下一步（需要开发者决策）

**目标**：把整对象 Enricher 的插入位置下移，越过 `Sort` / `Filter` 等只做行数削减的屏障，但仍留在 `Aggregate` 之前。预计把 complex-7 中 `message` 的 `VertexValue` 构造量从 1944 行降到 542 行（约 3.6x），对应 §8.8 中 S2（+39.2ms）与 S4（+25.9ms）里属于整对象物化的部分。

**为什么需要评审**：改动面在 Enricher 插入位置（`memo.cpp` 的 enforcer 生成、`physical_planner.cpp` 的 `materializeChosen` / `applyEnrichInPlace`），属于执行器核心。本会话对相邻机制（`lowerAliasPassthrough` 需求语义、collect 状态）的两次大改动都因触及承重不变量而回滚，因此该改动应在明确设计评审后进行。

**必须处理的细节**：整对象需求下移时，`Sort` / `Filter` 自身需要的扁平属性需求必须留在上游 —— 即需求要按屏障切成两段（聚合前 / 聚合后）。这正是原始 (A) 表述「phase-aware demand」的实质，但作用对象是 **Enricher 位置**而非需求本身。

**验收口径**（与 §8.2 / §8.6 一致）：
- `query_executor_tests` 519/519、`optimizer_tests` 109/109、`cypher_parser_tests` 81/81；
- TCK 15815 step 全过且与基线逐条 diff 为 0；
- 与 Neo4j 在 pid 933 / 1242 / 2199023256816 上逐行结果一致；
- 收益必须用**同一二进制 + 环境开关背靠背交替测量**判定（见 §8.2 的负载告警与 §8.6 中「负载假象」的教训）。

**已排除的替代路径**（避免重复尝试）：限制下游 `collect` 状态（§8.6）、只取消 `WITH n` 的 whole-object 需求（§8.6）、`Value&&` 重载（§8.6）、typed Sort 输出（§8.6）、Aggregate scratch 复用（§8.6）、只限制 `ConstructVertex` 的属性列表（§8.8：属性读取量本就不是瓶颈）。

### 8.10 与 Neo4j 执行计划的结构对照（第 7 轮）

Neo4j 的 complex-7 等价值执行计划（`EXPLAIN`，pid=2199023256816，`WHERE (message:Comment OR message:Post)`；自底向上）：

```text
NodeByLabelScan(person:Person)                       rows≈1528
  Filter person.id = $personId                       rows≈76
    Expand(All) (person)<-[:HAS_CREATOR]-(message)   rows≈14337
      Filter message:Comment|Post                    rows≈10763
        Expand(All) (message)<-[:LIKES]-(liker)      rows≈4130
          Filter liker:Person                        rows≈4130
            Projection like.creationDate AS likeTime
              Projection toInteger(message.id)
                Sort likeTime DESC, toInteger(message.id) ASC
                  EagerAggregation liker, person, collect({msg: message, likeTime}) rows≈64
                    Projection head(anon_2) AS latestLike
                      Projection latestLike.likeTime
                        Sort latestLike.likeTime DESC
                          Projection head(anon_2)
                            Projection liker.id, toInteger(personId)
                              PartialTop ... LIMIT 20        rows=20
                                LetAntiSemiApply
                                  Expand(Into) (liker)-[:KNOWS]-(person)
                                  Argument person, liker
                                CacheProperties cache[liker.lastName], cache[liker.firstName]
                                  Projection ... mL, c, mid
                                    ProduceResults
```

**两处结构性差异（都可量化）**

**① `message` 全程不被物化 —— Neo4j 用 slot 引用，聚合里也存引用**

Neo4j 的 `message` 从 `Expand(All)` 产出后，穿过 `Projection`、`Sort`、`EagerAggregation` 的 `collect({msg: message, ...})`、`head()`、再穿过外层 `Sort`，**始终是一个 node 引用（slot）**；只有到最上层 `Projection` 真正读取 `latestLike.msg.id / .creationDate / .content / .imageFile` 时才做属性访问，并由显式的 `CacheProperties` 只缓存**用到的两个** liker 属性。

EuGraph 的同一段计划里，`message` 在 `Expand(person→message)` 之后立刻被 `ProjectionExtract` 的 `ConstructVertex` 物化为完整 `VertexValue`（含全部属性容器的 `unordered_map`），因为 `collect({msg: message})` 的 map 必须存**具体值**；随后这个对象还要被 `Sort` 与聚合搬动。

**这是 EuGraph 与 Neo4j 在 complex-7 上最大的结构性差距**，也解释了 §8.8 的全部实测：瓶颈不是「属性读多了」，而是「在行数还很多的时候（1944 行）就把引用升级成了重型对象」。Neo4j 把这次升级推迟到了只有 20 行的时候。

**② pattern expression 的求值位置差 27 倍**

| | 位置 | 求值次数 |
|---|---|---|
| Neo4j | `LetAntiSemiApply` 位于 `PartialTop ... LIMIT 20` **之上** | **20 次** |
| EuGraph | `PatternComprehensionApply` 位于 `Sort(items=2)` 与 `Limit(20)` **之下**（紧贴聚合输出） | **542 次** |

Neo4j 让 `not((liker)-[:KNOWS]-(person))` 只在最终 20 行上求值；EuGraph 在当前计划里对全部 542 个分组各求值一次。§8.4 的 existence-only 提前退出已经把**每次**求值的成本降下来了，但次数差仍在。把 `BoundPatternComprehensionApplyOp`（或其后的 Apply）上移到 `Limit` 之上是另一个独立的优化点 —— 该 att 需要把 whole-object 需求（`liker`）与 `latestLike.likeTime`（排序键）一并上移，属计划重排。

**结论**：Neo4j 的两点做法指向同一个原则 —— **延后「引用 → 重型对象」的升级，并把它放到行数最少的位置**。EuGraph 需要的是 §8.9 的 Enricher 下移（对应 ①）与 pattern-apply 上移（对应 ②）。

### 8.11 第 10 轮：`need_entire` 溯源（一个被证伪的定位）

**假设**：`requirement_collector.cpp` 的「Project 裸转发 → `need_entire = true`」是迫使 complex-7 在 1944 行处构造整 `VertexValue` 的根因；把它限制到「结果投影」即可让中间 `WITH` 只产出扁平属性。

**实验**：
1. 先**完全删除**该规则 → `query_executor_tests` 从 519 降到 **518**，唯一失败是 `QueryExecutorTest.TckWith7Scenario1BoundEndpoint`（`WITH a AS b, b AS tmp, r AS r / WITH b AS a, r / LIMIT 1 / MATCH (a)-[r]->(b) / RETURN a, r, b`，期望 b=vid302 实际得到 301）。说明该规则对**别名链在槽位上的消歧**是承重的，不能整体删除。
2. 再改为**仅对结果投影生效**（`isResultProjection` 沿 `Limit`/`Skip`/`Sort`/`Distinct` 向下判定最外层 Project）→ `query_executor_tests` **519/519**、`optimizer_tests` **109/109** 全绿。

**结论：改动是语义惰性的，收益为 0。** 用 `EUGRAPH_DPL_DEBUG` 对比需求转储，改动**前后完全一致**：

```
[DPL] slot=3 var=message whole_vertex=true ... coalesce=1     ← 未变
```

即 complex-7 里 `message` 的 `whole_vertex` **不是**由「裸转发」规则触发的。同二进制 + 环境开关背靠背 A/B 也确认：旧行为 min 118.46 / med 122.34，新行为 min 122.43 / med 131.71 —— 差异属负载噪声（且方向不利），**已回滚**。

**这修正了 §8.9 的落点判断**：`need_entire` 的真实来源是 **`message` 被存进 `collect({msg: message})` 的 map**（`Project` 项里的裸引用，位于**聚合的输入投影**，不是结果投影），其外层消费是内层 Project 别名链的独立位置。因此 §8.9 的方案 (1) 不能靠改 `need_entire` 规则实现，**必须真正移动构造点**（把整对象构造放到行数削减之后）。这仍需要改 Enricher 插入层级，且该层级信息在 `collectPlanRequirements` 里不可见 —— 需求是按变量全局合并的，不区分「该需求来自 Sort 之前还是之后」。

**对「一劳永逸」方案的表述**：需要让需求收集**按算子位置分区**（同一变量在 `Sort` 之上的消费者需要整对象、在 `Sort` 之下的只需要扁平属性），而不是按变量全局合并。这是 §8.9 方案 (1) 的正确形态，也是本会话唯一尚未尝试过的实现路径。

### 8.12 设计评估：问题不在 ProjectionExtract，在需求模型的「位置无关性」

第 10 轮实验（§8.11）与第 1 轮实验（§8.6）合起来指向一个结论：**属性提取下推本身不复杂也不僵化，僵化的是它上游的需求模型。**

**证据**

| 观察 | 说明 |
|---|---|
| §8.8 C1 vs C2：map 装整 vertex vs 只装扁平属性，min 124.22 vs 124.35 | 扁平属性提取路径**工作正常且已自动生效**（`whole_vertex=false` + `vprop-coalesce`），没有僵化问题 |
| §8.6 第 1 条：取消 `WITH n` 的 whole-object 需求 → 519→495（24 失败） | 失败原因是约 52 处消费者直接 `std::get<VertexValue>`，**不是**提取能力不足 |
| §8.11：把裸转发规则限定到结果投影 → 519/519 但需求转储**完全不变**、收益 0 | 真实构造决策**不经过**该规则 |

**僵化点**：`PlanRequirements` 是 `unordered_map<SlotId, VariableRequirement>` —— **一个变量一个需求、一个构造点**。于是：

- 同一变量在计划**不同位置**的消费者被合并成一个需求；
- 只要**任一**消费者需要整对象，整对象就在**产出该变量的算子**（计划中行数最多处）被构造；
- 位置信息（「这个需求来自 `Sort` 之前还是之后」）在合并时丢失，下游无法据此把构造点后移。

这正是 Neo4j 计划（§8.10）与我们的根本差异：Neo4j 全程持引用、在最上层才读属性；我们则因为在 ALIAS/容器位置需要「对象」而把构造提到最前。

**«一劳永逸» 的正确形态**：让需求**按算子位置分区**，而不是按变量全局合并 ——

```
现在:  PlanRequirements = { slot -> Requirement }              // 位置无关
目标:  PlanRequirements = { slot -> { region -> Requirement } } // 按屏障分区
```

其中 region 由「纯行数削减屏障」划分（`Sort` / `Filter` / `Limit`）。规则：**某变量在屏障之上的消费者需要整对象时，不在产出算子构造，而在该屏障之后、该消费者之前构造**；屏障之下的消费者只声明它们真正读的扁平属性。

这样：
- 构造点自动落在行数最少的位置（complex-7：`message` 从 1944 行降到 542 行，甚至更后）；
- 不需要改那 52 处 `VertexValue` 消费者（它们在屏障之上，仍然拿到对象）；
- `ProjectionExtract` 的提取逻辑**无需改动** —— 它已经是按需的，只是被告知的需求粒度错了。

**落点**：`requirement_collector.cpp`（收集时携带 region 而不是直接 `mergeVarRequirements`）、`column_rewrite.hpp` 的 `PlanRequirements` 类型、`column_rewrite.cpp` 的 `collectPlanRequirements` / `buildExtractionInfo`、以及 `memo.cpp` 的 enforcer 生成（按 region 选构造位置）。

**为什么没有在本会话实施**：这是 `PlanRequirements` 的语义变更，牵动 DPL 全部下游（`lowerAliasPassthrough`、`rewriteExpr`、`dispatchProjectionExtract`）。本会话在同类承重语义上已失败 3 次（§8.6 两条 + §8.11），且该改动必须一次做对才能保住 519/519 与 TCK 0 变化 —— 需要一轮完整预算独立实施与验证。

### 8.13 COMPLEX-7 剩余成本归因与实现障碍（第 11–12 轮）

**每查询分配次数逐阶段归因**（`LD_PRELOAD` 计数器，40 次查询取平均，pid=2199023256816）

| 阶段 | alloc/query | 时间 | 相对上一阶段 |
|---|---:|---:|---:|
| S1 纯三段展开 | 158,369 | 23.6ms | — |
| S2 + 物化 `liker, message, likeTime, person` | 777,368 | 65.1ms | **+619,000** |
| S3 + ORDER BY | 991,402 | 73.4ms | +214,033 |
| S4 + `head(collect({msg, likeTime}))` | 1,703,248 | 99.6ms | **+711,847** |

完整 complex-7 约 234 万次分配/查询。**S2 与 S4 是同一根因**（`message` 作为整对象被构造与拷贝），合计约 133 万，占总量 57%。S1 说明「读图」本身只占 15.8 万 —— 问题在物化，不在存储。

**已实施的优化（第 11 轮）**

`ExpandPhysicalOp` 复用跨 chunk 的 `edges` / `allowed_seen` 缓冲，并删除 `chunk->toRows()`（原先为每个输入行构造一个 `std::vector<Value>`，而代码只读源点与绑定端点两列，改为直接 `chunk->columns[c].getValue(row)`）。同二进制 + 环境开关 A/B，两轮各 30 次：min **135.31/136.21 → 127.47/128.03**、med **139.41/143.02 → 132.18/131.63**（约 -6%）。

**同时修掉一个算法级缺陷**：`ExpressionEvaluator::evalMap` 把 entry 求值放在逐行循环内，而 `acquireTempColumn` 只向 `std::deque` 追加、从不复用，于是 2-entry map 在 1024 行 chunk 上分配约 2048 个临时列而非 2 个（`O(rows × entries)`）。改为每 chunk 求值一次后：`collect({msg: message.id, likeTime: likeTime})` 由 **27,965,716 → 1,445,761** alloc/query、**524.92 → 86.89 ms**（分配 -19x、时间 6x）。注意：**该缺陷不影响 complex-7**（其 map 的两个值都是普通列引用，走非分配路径），complex-7 上 A/B 为平（min 120.37/119.25 vs 120.63/126.50）。

**剩余杠杆的确切实现障碍**

要消除 S2/S4 的整对象物化，必须让容器里的 vertex 保持引用/扁平属性而非整对象。障碍已定位到一处入口：

`src/query/optimizer/column_rewrite.cpp` 的 `rewriteExpr()` 属性引用分支第一步即
```cpp
std::string var = varNameFromObject(val->object);
if (var.empty()) return false;          // complex-7 走到这里即返回
```
而 `varNameFromObject()`（同文件 49 行起）只识别 `BoundVariableRef` / `BoundColumnRef` / `BoundLabelCast` / `startNode` / `endNode`。complex-7 聚合后的访问是 `latestLike.msg.creationDate` —— object 为 **map 下标**，返回空，因此整条属性引用不做扁平列 lowering，运行时只能从构造出的对象里取属性。

**失败模式（必须在设计中处理）**：若只把 map 里的 vertex 改为存 `VertexRef`（该类型本就在 `Value` variant 内，类型合法）而不打通上述 lowering，则 `.creationDate` / `.content` / `.imageFile` 这类非结构属性将**静默返回 null**（`VertexRef` 仅携带 id；`evalPropertyRef` 只对 `id` 有结构字段兜底）。这类错误不一定被单元测试捕获，必须靠与 Neo4j 逐行对比才能发现。

**最小路径**：① 让 `BoundMap` 记录「某 entry 的值来源于哪个变量」的来源元信息；② 在 `rewriteExpr` 中，当 `BoundPropertyRef` 的 object 是「源自变量 X 的 map 下标」时，使用 X 的 PEPlan 扁平列槽位做替换；③ **仅在完整可解析时替换**，否则保守回退到现状（这是避免静默 null 的关键约束）；④ 验收：519/519 + TCK 逐条 diff 为 0 + 与 Neo4j 在 933/1242/2199023256816 逐行一致 + 同二进制 A/B 量化。

**第 12 轮另两项实测（均为负结果，已回滚，记录以免重试）**

| 尝试 | 结论 |
|---|---|
| 复用 edge-scan cursor（新增 `IEdgeScanCursor::reposition()`，整批源点共用一个 cursor） | 无收益且可能损害邻接局部性（WT 自身 cursor cache 已摊销按顶点建 cursor 的开销）。首次读数看似回归，实为机器负载漂移 —— 与 §8.6 记录的同一陷阱 |
| `Column::setValue(Value&&)` 搬入重载（消除 `ConstructVertex` 处 `Value(*cache[row])` 的第四次整对象拷贝） | 同二进制 + 环境开关 A/B 两轮：拷贝路径 min 125.62/125.40 / med 132.30/130.58，搬入路径 min 125.49/126.05 / med 130.76/129.20 —— **平的**。说明该处拷贝不是可摘的果子 |

**第 12 轮补充实测：缩减 `ConstructVertex` 的属性列表也无收益**

既然 `need_whole_vertex` 分支显式丢弃了 `r.vertex_props`（`column_rewrite.cpp` 中「A whole-vertex Construct column must carry every property」），一个自然的想法是：既然唯一消费者只读 4 个属性，就让 `ConstructVertex` 只加载这 4 个。实测否定（同一查询骨架，`/proc/<pid>/stat` 计服务端 CPU，40 次/例）：

| WITH 内容 | 服务端 CPU/查询 | wall/查询 |
|---|---:|---:|
| 4 个裸引用（`message` 整对象） | **73.75 ms** | 62.65 ms |
| 只有 `message.id`（扁平） | 77.50 ms | 65.61 ms |
| `message` 的 5 个扁平属性 | 85.00 ms | 72.74 ms |

**整对象反而最快。** 因此「少读属性」这条路（含 §8.8 的 C1/C2 对照、§8.6 的 typed 列尝试）已被三种独立测法一致否定：属性读取量不是成本，构造与逐行算子开销才是。

**第 12 轮结论：complex-7 在现有执行模型下已无低成本增量可摘。**
已实测否定的方向累计 9 个（§8.6 六条 + §8.11 + 本节两条 + 搬入重载）。剩余差距的唯一出口是 §8.13 描述的容器下标 lowering（让 `latestLike.msg.<prop>` 能被 lower 成扁平列，从而容器里无需存整对象），其实现障碍与失败模式已在该节写明；该改动属编译期数据流能力，需要独立一轮实现并靠 519 + TCK + 与 Neo4j 逐行对比守住正确性。

### 8.14 移除 `DataChunk::toRows()` 与 DISTINCT 的一个真实语义缺陷（第 13 轮）

**重构**：`DataChunk::toRows()` 已从生产 API 删除。它按行物化 `std::vector<Value>`（每行一次堆分配）并复制每个单元格，而 9 个调用点里：

- **4 个算子**只读 1~2 列（`distinct`、`varlen_expand`、`path_build`、`path_element_property_read`）→ 改为直接 `chunk->columns[c].getValue(r)`；`path_element_property_read` 原先还回写行数组，改为按列写 `path_out`；
- **2 个输出边界**（Bolt `handlePull`、Thrift `eugraph_handler`）只需逐行序列化 → 改为按列遍历、就地构造 packstream/thrift 值，省掉每行一个 `std::vector`；
- **1 个遗留桥接**（`dataChunkToRowBatch`）→ 直接从列构造行；
- 测试侧改用本地 `chunkToRows()` 辅助函数。

未引入 `RowView`：`RowView::operator[]` 的实现与直接 `getValue` 完全相同，却要给 4 个算子引入「chunk 被 move/yield 后 view 悬垂」的新风险类别。

**顺带发现并修复一个真实语义缺陷（严重）**

重写 `distinct` 时暴露出：`MATCH (p:Person)-[:KNOWS]-(f:Person) WITH DISTINCT f RETURN count(*)` 在**原实现下语义也是错的**。

- 原 `DistinctPhysicalOp` 把整行 `std::vector<Value>` 塞进 `unordered_set<Row, RowHash, RowEqual>`。`RowEqual` 用 `std::variant::operator==`，它**先比 variant 下标**，因此同一顶点的 `VertexValue` 与 `VertexRef` 两种表示被判为不同 → 无向遍历产生的两个端点不会合并。
- 实测：`WITH DISTINCT f` = **2514**，而 `count(DISTINCT f)` = **1357**（Neo4j 亦为 1357）。同一语义两种写法不一致，且 `WITH DISTINCT f` 正是 519 单测未覆盖的形状。
- 修复：键改为**即时计算的逐列指纹**（不是持有 chunk 引用的视图），实体单元格取 **id**（与 `ValueHash` 一致，`VertexRef`/`VertexValue`/`EdgeKey`/`EdgeValue` 都归一到 id），非实体走 `ValueHash`。
- 修复后与 Neo4j 逐一对照全部一致：

| 查询 | eugraph | Neo4j |
|---|---:|---:|
| `WITH DISTINCT f`（无向） | 1357 | 1357 |
| `WITH DISTINCT f`（单向） | 1205 | 1205 |
| `WITH DISTINCT p, f` | 14073 | 14073 |
| `WITH DISTINCT f.id` | 1205 | 1205 |
| `VLE *2` 路径数 | 2732 | 2732 |
| `VLE *1..3` 去重 | 1034 | 1034 |

**设计约束（踩过的坑）**：第一版键持有 `const DataChunk*` + row，键的生命周期比 chunk 长（chunk 按值 yield 后即销毁）→ 悬垂指针，实测触发 `std::bad_alloc`。因此键必须在读取行时**即时算出并只存数值**。

**实测收益**（服务端 CPU 取自 `/proc/<pid>/stat`，两轮各 20 次，前后各测两遍）

| 查询 | before（含 `toRows`） | after（无 `toRows`） | 变化 |
|---|---:|---:|---:|
| `WITH DISTINCT p, f`（14k 行） | 178.0 / 178.5 ms | **137.5 / 143.0 ms** | **约 -22%** |
| short-2 | 4 / 5 ms | 4.5 / 5 ms | 持平 |
| complex-4 | 21 / 20 ms | 19 / 19 ms | 约 -5% |

DISTINCT 受益最大，因为它原先要为每行物化整行 `std::vector<Value>` 再塞进哈希集合 —— 现在只算逐列指纹。
