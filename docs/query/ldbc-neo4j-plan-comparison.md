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

1. c14 PC 融合 + 锚点下推（当前 119.5 ms，目标 <100 ms）；
2. c5 vertex-list semi-join（当前 246.5 ms，目标 <150 ms）；
3. c9 日期索引/反向 join（当前 177.1 ms）。
