# LDBC c5/c9/c14：neo4j 执行计划对比

> 2026-10-27，CsvComposite sf0.1；neo4j `PROFILE`（plan 形状/dbHits 可信，
> profile 的 wall time 有额外开销，不直接和 eugraph 计时比较）。

## complex-14

neo4j：

```text
ShortestPath(path=(person1)-[:KNOWS*0..]-(person2))  4 rows
  EagerAggregation(collect path)
    Projection:
      [n IN nodes(path) | n.id]
      [r IN rels_in_path | reduce(... COLLECT { MATCH
         (a)<-[:HAS_CREATOR]-(comment)-[:REPLY_OF]->(...)-[:HAS_CREATOR]->(b)
         WHERE a.id = startNode(r).id ... } ...)]
      dbHits = 142,101
```

eugraph：

```text
ShortestPath
  PatternComprehensionApply × 2
    CorrelatedSource → Filter → CorrelatedVertexProbe
    Expand(a <-HAS_CREATOR)
    Expand(-> REPLY_OF)
    Expand(-> HAS_CREATOR b)
```

差异与优化：

1. neo4j 每条 path 只跑一个 `COLLECT` 子查询；eugraph 两个 PC 各跑一次展开；
   可先算 `a` 的 comment 集合，再按 `REPLY_OF` 目标类型分流，共享前缀。
2. `a.id = startNode(r).id` / `b.id = endNode(r).id` 目前落在 Filter；
   可下推为 Expand/Probe 的端点锚定，避免先展开全部 comment/post 再过滤。
3. 批量顶点读取已把 c14 的 dispatch 从 91,672 降到 696；剩余主要是展开中间行。

## complex-5

neo4j：

```text
NodeIndexSeek(Person)
VarLengthExpand(Pruning,BFS) 174
Filter
Expand HAS_MEMBER 29,894
Filter joinDate
EagerAggregation
Optional:
  Expand CONTAINER_OF 89,415
  Expand HAS_CREATOR 89,415
  Filter friend IN friends
```

eugraph 形状相同（LeftJoin 按 512 行批处理 + 共享 cursor）。
差距不在计划选择，而在 89k posts/creators 的 per-edge 成本。
优化方向：用 `friends` 集合做 vertex-list semi-join，先过滤 HAS_CREATOR，
再展开 CONTAINER_OF，减少 89k 中间行。

## complex-9

neo4j：

```text
NodeIndexSeek(Person)
VarLengthExpand 174
Filter
Expand HAS_CREATOR 53,192
Filter creationDate < maxDate  (60,898 dbHits)
Top 20
CacheProperties (only 20 rows)
```

eugraph 已做 LIMIT 边界延迟物化 + coalesce 批量属性读取；
seek 约 44 ms，剩余主要是 53k message 的扫描/排序/输出。
优化方向：`Message.creationDate` 索引或从 Filter 结果反向 join，减少 53k 属性读取。

## 优先级

2026-10-29 更新：c5/c9 的“过滤前物化”已由 Filter 边界延迟物化消除
（c5 242→159 ms，c9 177→109 ms，见
[query-performance-optimization-notes.md](../benchmark/query-performance-optimization-notes.md#812-2026-10-29-第二轮filter-边界物化--实体引用不物化)）。
剩余差距在 per-edge / per-row 成本：

1. c5：Filter 前仍是 89,415 条 CONTAINER_OF + 89,415 条 HAS_CREATOR 的 Expand（约 126 ms CPU）。
   下一步做 `dst IN list` 下推进 Expand，或从 174 个 friend 反向展开后与 forum 半连接；
2. c9：53k 条 HAS_CREATOR 边 + creationDate coalesce（id 预取已消除）；
3. c14：两路 PC 各约 57 ms，anchor 已基本下推，剩余是 per-edge Expand/PE，需单独拆解。
