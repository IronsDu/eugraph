# LDBC SNB Interactive SF0.1 对比测试与慢查询分析

> 分支：`fix/ldbc-snb-slow-queries`
> 数据：`social_network-sf0.1-CsvBasic-LongDateFormatter`（LDBC Datagen 生成，sf0.1）
> 查询集：`ldbc_snb_interactive_v1_impls` 仓库 `cypher/queries/` 中的 14 条 complex + 7 条 short
> 时间口径：单次执行、未预热，单位为毫秒（ms）。除特别说明外，EuGraph 查询为“等价改写”后的版本（关系类型 camelCase、无 `:Message` 标签）。
> 测试日期：2026-09-05 ~ 2026-09-06

## 1. 数据导入

| 引擎 | 数据目录 | 节点数 | 边数 | 导入耗时 |
|------|----------|-------:|------:|---------:|
| EuGraph | `eugraph-sf0.1-ldbc-data`（转换后 LDBC schema） | 327,588 | 1,477,965 | ~20s |
| Neo4j 5.26.30（本地独立实例） | `neo4j-local/data` | 327,588 | 1,477,965 | 8.4s |

EuGraph 导入使用 `eugraph-loader`，Neo4j 使用 `neo4j-admin database import full`。
Neo4j 索引/约束来自 LDBC cypher 实现仓库的 `scripts/indices.cypher`（导入后手动执行）。
EuGraph loader 自动为每个标签的 `id` 属性建唯一索引；`creationDate`/`name` 等索引是测试中后期手动补建的。

## 2. Neo4j 原版查询结果（21 条全部跑通）

| Query | 行数 | 耗时(ms) |
|-------|-----:|---------:|
| complex-1  | 1  | 362.9 |
| complex-2  | 20 | 108.9 |
| complex-3* | 0  | 342.2 |
| complex-4  | 10 | 105.1 |
| complex-5  | 20 | 292.1 |
| complex-6  | 10 | 151.2 |
| complex-7  | 20 | 160.9 |
| complex-8  | 20 | 83.7  |
| complex-9  | 20 | 120.9 |
| complex-10 | 10 | 243.4 |
| complex-11 | 10 | 86.4  |
| complex-12 | 2  | 364.1 |
| complex-13 | 1  | 43.9  |
| complex-14 | 1  | 273.0 |
| short-1    | 1  | 36.5  |
| short-2    | 10 | 63.2  |
| short-3    | 3  | 34.9  |
| short-4    | 1  | 25.6  |
| short-5    | 1  | 26.5  |
| short-6    | 1  | 48.5  |
| short-7    | 0  | 67.9  |

\* complex-3 首次用 `Germany/India` 参数返回 0 行；后续换 `Germany/Brazil` 返回 1 行（friendId=987），时间未重测。

## 3. EuGraph 已拿到的结果

EuGraph 无法直接执行原版 LDBC 查询，原因见第 5 节。以下为等价改写后的结果。

| Query | 行数 | 耗时(ms) | 备注 |
|-------|-----:|---------:|------|
| complex-1 | 1  | 352.6 | 改写 `shortestPath` 为 `[:knows*1..3]` + `min(length(path))` |
| complex-2 | 20 | 31.4 | 改写 `:Message` 与关系类型 |
| complex-3（优化版） | 1  | ~2500 | 见第 6 节 |
| complex-4 | 10 | 4599.9 | 见第 6 节 |
| complex-5 | 超时 | >60000 | CPU/内存失控，未完成 |
| complex-6 | 超时 | >60000 | CPU/内存失控，未完成 |
| complex-7 | 超时 | >60000 | 核心聚合部分单独跑 0.11s；OPTIONAL MATCH 组合后失控 |
| complex-8 | 20 | 139.7 | 改写 |
| complex-9 | 未完成 | - | 测试中断，未拿到结果 |
| complex-10 | 不支持 | - | pattern comprehension 语法报错 |
| complex-11 | 10 | 34.2 | 改写 |
| complex-12 | 未完成 | - | 测试中断，未拿到结果 |
| complex-13（有界 1..3 近似） | 1 | ~370 | 见第 6 节 |
| complex-14 | 不支持 | - | `allShortestPaths` + `reduce` 语法报错 |
| short-1 | 1 | 46.9 | 改写 |
| short-2 | 10 | 9314.0 | 见第 6 节 |
| short-3 | 3 | 17.4 | 改写 |
| short-4 | 1 | 953.9 | 改写 |
| short-5 | 1 | 1128.3 | 见第 6 节 |
| short-6 | 1 | 1143.0 | 见第 6 节 |
| short-7 | 0 | 1115.1 | 见第 6 节 |

## 4. EuGraph 兼容性 / 稳定性问题清单

1. **语法不支持**：`shortestPath`、`allShortestPaths`、`reduce`、pattern comprehension。
   受影响：Q1、Q10、Q13、Q14 无法按原版执行。
2. **函数缺失**：`floor` 不存在。Q7 已改写为 `toInteger((likeTime - msg.creationDate) / 60000.0)`。
3. **loader 文件名解析限制**：关系文件名必须恰好 5 段（`src_edge_dst_0_0`），关系类型不能含下划线。
   因此 LDBC 原版 `HAS_CREATOR` 等关系类型必须改写成 camelCase `hasCreator`。
4. **多标签支持缺陷**：loader 只创建单标签；`SET n:Message` 会添加空标签而不是 `Message`（该 SET 标签名丢失 bug 已在当前分支修复；loader 单标签仍是 benchmark 里 `:Message` 查询需改写的原因）。
   因此 `:Message` 查询只能改写为 `WHERE m:Comment OR m:Post`。
5. **Binder bug**：`OPTIONAL MATCH WHERE` 中“节点等值 + 日期比较”同时出现时报 `UndefinedVariable`。
   Q3 通过把日期条件移出 WHERE 规避。
6. **稳定性问题**：Q5/Q6/Q7 在 EuGraph 上出现 CPU 100%+、内存 1.8~5.6GB 的失控查询，机器卡死，需 kill server 恢复。
   这些查询在 Neo4j 上均不超过 300ms。

## 5. 需要重点分析的慢语句

以下查询为“能执行但明显慢于 Neo4j”的语句，第 6 节逐条分析。

| Query | EuGraph(ms) | Neo4j(ms) | 倍数 |
|-------|------------:|----------:|-----:|
| complex-3（优化版） | ~2500 | ~342 | ~7x |
| complex-4 | 4599.9 | 105.1 | ~44x |
| complex-13（有界 1..3 近似） | ~370 | 43.9（原版） | ~8x |
| short-2 | 9314.0 | 63.2 | ~147x |
| short-5 | 1128.3 | 26.5 | ~43x |
| short-6 | 1143.0 | 48.5 | ~24x |
| short-7 | 1115.1 | 67.9 | ~16x |

## 6. 慢查询分析

> 方法：对 EuGraph 改写后的慢查询执行 `EXPLAIN`（只生成计划、不执行），统计物理算子。
> 关键算子：`AllNodeScan`（全节点扫描）、`IndexScan`（标签索引扫描）、`LabelScan`、`Expand`、`VarLenExpand`、`CrossProduct`、`LeftJoin`。

### 6.1 complex-4（4599.9ms）— 单 MATCH 多 pattern 未做关联 join

原改写查询是一个 MATCH 里两个共享变量 `friend` 的 pattern：

```cypher
MATCH (person:Person {id:$personId})-[:knows]-(friend:Person),
      (friend)<-[:hasCreator]-(post:Post)-[:hasTag]->(tag)
```

`EXPLAIN` 显示物理计划是两个子计划的 **CrossProduct**：

- 左分支：`IndexScan/ LabelScan(person)` → `Expand(person→friend)`
- 右分支：**`AllNodeScan(friend)`** → `Expand(friend→post)` → `Expand(post→tag)`

最后在 CrossProduct 之上用 Filter 做 friend 等值过滤。右分支把 `friend` 当成未绑定变量，全节点扫描 327,588 节点后再展开到 post/tag，这就是 4.6s 和内存高的原因。

**根因**：Binder 已区分 `CARTESIAN` 与 `CORRELATED`，但物理层没有把“共享 friend 的第二个 pattern”计划成以左分支 `friend` 为输入的关联展开，而是退化为独立扫描 + CrossProduct + Filter。

**验证**：把查询人工拆成 `MATCH ... WITH ... MATCH ...`（显式关联）后，`EXPLAIN` 显示 `IndexScan(person)` → `Expand(person→friend)` → `Expand(friend→post)` → `Expand(post→tag)`，不再出现 AllNodeScan。

### 6.2 short-2（9314.0ms）— WITH 后第二个 MATCH 的两 pattern 同样退化为 AllNodeScan(post)

查询第二个 MATCH：

```cypher
MATCH (message)-[:replyOf*0..]->(post:Post),
      (post)-[:hasCreator]->(person)
```

`EXPLAIN` 显示计划底部有 **`AllNodeScan(post)`**，即第二个 pattern `(post)-[:hasCreator]->(person)` 从全节点扫描 post 开始，而不是从第一个 pattern 产出的 `post` 继续 Expand。这导致扫描全部 135,701 个 Post，并做 CrossProduct 后过滤。

**根因**：与 6.1 同类——单 MATCH 内多个 pattern 之间的关联变量没有被物理计划用作关联输入。

**验证**：人工拆成 `MATCH ... WITH ... MATCH (post)-[:hasCreator]->(person)` 后，计划变为 `LabelScan(Person)` → `Expand` → `Sort/Limit` → `VarLenExpand(message→post)` → `Expand(post→person)`，无 AllNodeScan。

### 6.3 short-5 / short-6 / short-7（约 1.1s）— 无 `:Message` 标签导致 `m {id:...}` 退化为 AllNodeScan

改写查询使用 `MATCH (m {id:$messageId}) WHERE m:Comment OR m:Post`。由于 EuGraph 不支持多标签，物理计划无法把 `m` 归结为 `LabelScan` 或 `IndexScan`，只能：

- `AllNodeScan(m)`（327,588 节点）
- `Filter`（`m:Comment OR m:Post`）
- `Expand` 到 creator/forum 等

Neo4j 原版 `(m:Message {id:$messageId})` 会命中 `Message(id)` 约束索引，因此只要 25~50ms。

**根因**：EuGraph 缺少多标签能力（loader 单标签 + `SET n:Message` 会添加空标签），导致 benchmark 改写只能使用无标签点查。

**可行方案**（按优先级）：
1. 实现真正的多标签支持，导入/添加 `Message` 标签，让 `m:Message {id:...}` 走索引。
2. 查询改写用 `UNION ALL` 两个标签索引扫描（`m:Comment {id:...}` + `m:Post {id:...}`），不再全节点扫描。
3. 在物理层支持“多标签 OR 条件”的标签剪枝，至少把 AllNodeScan 换成两个 LabelScan 的 Union。

### 6.4 complex-3（优化版，~2500ms）— 多个 Expand + LeftJoin，缺边索引与 join order

`EXPLAIN` 显示：`IndexScan`×3、`LabelScan`×1、`Expand`×7、`LeftJoin`×2、`Aggregate`×3，计划 269 行。瓶颈主要是：

- `MATCH (person)-[:knows*1..2]-(friend)-[:isLocatedIn]->(city)` 的变长展开；
- 两个 `OPTIONAL MATCH` 通过 LeftJoin 展开 comment/post；
- 无 `Message(creationDate)` 这类“边/属性”以外的索引可进一步下推日期过滤（EuGraph 目前索引只作用于节点标签属性，不支持无标签点查）。

该查询虽然能跑（2.5s），但相比 Neo4j 342ms 仍慢约 7 倍。优化方向：join order 调优、varlen expand 剪枝、LeftJoin 右支延迟物化。

### 6.5 complex-13（有界 1..3 近似，~370ms）— 近似算法固有开销

计划是干净的：两个 `IndexScan(person1/person2)` → `VarLenExpand([1..3], KNOWS)` → `Aggregate(min)`。0.37s 主要花在 `VarLenExpand` 枚举两点之间所有 1..3 跳路径，而 Neo4j 的 `shortestPath` 是双向 BFS，找到最短路径即停止。

**根因**：EuGraph 没有 `shortestPath` 算子，只能用 `[:knows*1..3]` + `min(length(path))` 近似；该近似的成本随路径数量增长，且语义不是最短路径算法。

**优化方向**：新增 shortest path 物理算子（双向 BFS），而不是继续用 varlen + aggregate 近似。

### 6.6 小结

| 慢查询 | 根因 | 建议 |
|--------|------|------|
| complex-4 | 多 pattern 未关联 join，AllNodeScan + CrossProduct | 修 pattern join 物理计划 |
| short-2 | 同上（WITH 后第二个 MATCH 的两 pattern） | 同上 |
| short-5/6/7 | 无 `:Message` 标签，无标签点查退化为 AllNodeScan | 多标签支持或 UNION ALL 改写 |
| complex-3 | 多 Expand/LeftJoin + 缺 join order/边索引 | join order 调优、varlen 剪枝 |
| complex-13 | 无 shortestPath 算子，近似算法固有开销 | 实现 shortestPath |

