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
| complex-7 | 原版在 EuGraph 中语法不支持（RETURN 中的 pattern expression）；等价 OPTIONAL MATCH 改写已在第 7 节验证不再失控 |
| complex-9 | 已知超时（>40s），跳过 |
| complex-10 | pattern comprehension 语法不支持 |
| complex-14 | `allShortestPaths` + `reduce` 语法不支持 |

## 4. 当前版本关键优化（Q12 `bf25c759` + 本轮 Complex-3 / Short-7）

1. **Q12 计划重写**：`Apply(collect(tag.id), HashJoin(friend))`，左支从 `Tag(id) IN tags` 反向展开，右支从 `Person(id)` 经 KNOWS 展开。
2. **Expand 批处理**：dst label 一次批量检查；新增 `scanEdgesBatch` 快速路径。
3. **VarLenExpand 邻接缓存**：同一 chunk 内复用每个 vertex 的邻接边和 label 判定，避免多起点重复扫描同一张图。
4. **VarLenExpand OR 索引剪枝**：`WHERE src.prop = v OR dst.prop = v` 下推为 src/dst 索引允许集，Q12 左分支起点从 71 个 TagClass 降为 1 个。
5. **Complex-3 索引优先 join**：将 `Expand(friend→message) → Filter(message.creationDate) → Expand(message→country)` 改写为 `HashJoin(candidate friends, IndexScan(Message.creationDate range) → HAS_CREATOR → creator)`；日期谓词在 Expand 之上先拆分为独立 Filter 后再下推。
6. **Short-7 OPTIONAL MATCH 相关性链修复**：起点已绑定时不再因为终点也已绑定就退化为独立扫描 + CrossProduct，而是继续用 `CorrelatedSource → Expand(start→new) → Expand(new→bound_endpoint)`；同时修复了 complex-7 的 OPTIONAL MATCH 等价改写。

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
| complex-4 | ~2.0x | 已修多 pattern 关联；剩余在聚合/排序/ProjectionExtract | 聚合与排序批量化；ProjectionExtract 向量化 |
| complex-11 | ~2.3x | 同上 | 同上 |

### 5.3 仍需避免执行的语句

| 语句 | 现象 | 已定位方向 |
|------|------|-----------|
| complex-5 | CPU/内存失控 | 需先隔离复现，再做 bounded VLE / 去重 |
| complex-6 | 同上 | 同上 |
| complex-7 | 原版语法不支持；OPTIONAL MATCH 改写原会失控 | 本轮已修复 optional 相关性计划；原版 pattern expression 支持见第 7 节 |
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
- `query_executor_tests`：507/507 通过。
- SHORT-7 有回复（`messageId=893353237791`）：
  - 修复前：超时 >240s；
  - 修复后：19 行，同机中位数约 7.02ms；
  - Neo4j 同参数：19 行，中位数约 9.27ms；
  - 两边返回结果逐行一致。

### 7.4 COMPLEX-7 分析

原版 complex-7 在 EuGraph 中仍然报：

```text
SyntaxError: UnexpectedSyntax
```

原因是最后返回项中的 pattern expression：

```cypher
not((liker)-[:KNOWS]-(person)) AS isNew
```

目前 EuGraph 只允许 pattern expression 出现在 WHERE 中。

用等价的 OPTIONAL MATCH 改写：

```cypher
WITH liker, head(collect({msg: message, likeTime: likeTime})) AS latestLike, person
OPTIONAL MATCH (liker)-[r:KNOWS]-(person)
RETURN ..., r IS NULL AS isNew
```

在修复后的计划中右支变成：

```text
CorrelatedSource(liker, person)
  -> Expand(liker)-[r:KNOWS]-(person)   // person 为 bound destination
```

不再出现 `AllNodeScan + CrossProduct`，因此不会再 OOM。用 `personId=1242` 验证：

- EuGraph：20 行，结果与 Neo4j 原版逐行一致；
- 同机热态中位数：EuGraph 约 51.8ms，Neo4j 约 15.0ms；
- 原版 complex-7 的直接支持需要把 RETURN/ORDER BY 中的 pattern expression 统一 hoist 成 `PatternComprehensionApply + size(list) > 0`，可复用现有 `BoundPatternComprehensionApplyOp` 基础设施，作为后续特性实现。
