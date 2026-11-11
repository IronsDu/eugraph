# 相关端点：绑定目的端、批量精确查与集合探针

Status: 2026-11-11. LDBC complex-10 优化（sf1：同会话基线 490–508 ms → 277–300 ms，PC 子树 180 ms → 63 ms）。

## 1. 问题

模式推导的子计划里，端点变量来自外层时，binder 会为该端点分配一个子计划内的
`__exists_dst_N`，再在 Expand 之后用

```
Filter(id(__exists_dst_N) == id(__exists_saved_N))
  Expand(src=tag, dst=__exists_dst_N, ...)
```

把 Expand 产出的目的端与保存的外层值对齐。这带来两个代价：

1. **Expand 无法用端点约束**：目的端在 Expand 里是“新变量”，只能扫描 `src` 的整段邻接
   再过滤。c10 的 `(tag)<-[:HAS_INTEREST]-(person)` 因此枚举 261 万条 HAS_INTEREST
   （约 150 ms）。
2. **后续 Filter 无法消除**：即使改成逐行精确 seek，每行也要一次 cursor open + 两次
   IO dispatch（19,535 行 × ~28 µs = 556 ms）。

## 2. 三个机制

### 2.1 把端点等值折进 Expand（planBound）

`physical_planner.cpp` 的 `rewriteDstBoundExpandFilters` 在
`collectPlanRequirements` **之前**做结构化重写：

```
Filter(BinaryOp(EQ, id(ref_a), id(ref_b)))
  Expand(dst=ref_a)
```

→ `Expand(dst=ref_b)`，并删掉该 Filter。识别条件（防止误改用户谓词）：

- 两边都是 `id()` 函数调用，参数是 **`VERTEX_REF`** 类型的 `BoundColumnRef`：
  binder 生成的端点等值刻意使用拓扑阶段的 ref；用户写的 `id(a)=id(b)` 比较的是
  `VERTEX` 列，不会命中。
- 被替换的一端必须等于 Expand 的 `dst_variable`；另一端（`__exists_saved_N`）必须由
  子计划里的 `CorrelatedSource` 注入（`subtreeProvidesCorrelatedVariable` 校验）。
- Filter 直接位于 Expand 之上；重写后删掉 Filter、把 Expand 的
  `dst_variable / dst_column_index / dst_slot_id / planner_dst_slot_id` 指向保存列。

必须早于需求收集：dst 绑定会改变下游需要物化的列集合。重写只改计划形状，不依赖
只读性。

### 2.2 批量精确 seek（存储接口）

`ISyncGraphDataStore::scanEdgesExactBatch(session, queries, cb)` /
`IAsyncGraphDataStore::scanEdgesExactBatch(queries)`：

- 一个 `edge_index` cursor 服务整批 `{src}{dir}{label}{neighbor}` 查询；
- 每个命中带 **query index** 返回，调用方能映射回输入行；
- 异步实现每 4,096 条查询一次 IO dispatch（逐行版本是每行两次 dispatch）；
- 邻接缓存（`adjacency_cache_capacity_ > 0`）启用时回退逐 source 的 `scanEdges`，
  因为 COMPLETE 邻接不在 `edge_index` 里；
- `Direction::BOTH` 在批内拆成 OUT + IN 两次 seek。

`ExpandPhysicalOp` 的 dst-bound 分支用它替换逐行 `scanEdgesExact`；UNDIRECTED 时
OUT/IN 两趟查询后按行稳定排序，并按 edge id 去掉 IN 侧重复的自环，保持逐行路径的
顺序与去重语义。

### 2.3 绑定目的端的集合探针

对**有向**的相关端点（`probe_bound_dst`），翻转遍历方向：不再从每个 source 行 seek，
而是把目的端（person）的邻接**每批物化一次**，再对每个 source 行做 O(1) 成员判定。

- 计划标记：2.1 的重写在有向且 `ctx.read_only` 时设置 `BoundExpandOp::probe_bound_dst`；
  写语句不启用（集合缓存假设语句内快照不变）。
- 执行：`ExpandPhysicalOp::probeBoundDst`
  - 按目的端 id 缓存 `neighbor -> [edges]`（内层哈希表，行查找 O(1)）；
  - 每批只对未缓存的目的端调用一次 `scanEdgesBatch(dst_ids, flipped_dir, label)`；
  - 目的端 degree 超过 `kMaxProbeEdgesPerSource`（16,384）或总预算
    `kMaxProbeCachedEdges`（262,144）时不缓存，对应行回退批量精确 seek；
  - 每个匹配的邻接条目输出一行（保留并行边重数），
    `EdgeKey` 的物理方向按 `LEFT_TO_RIGHT` / `RIGHT_TO_LEFT` 还原，
    与逐行 seek 完全一致。
- 语义：与“从 source 精确 seek 目的端”是同一组边（有向时每条边只出现在目的端的一个
  方向表里）；UNDIRECTED 不走探针，继续用 2.2 的双向精确 seek + 自环去重。

## 3. c10 结果（sf1，personId=933，month=5）

| 指标 | 优化前 | 2.1+2.2 | +2.3 | +列表元素拓扑化 |
|---|---:|---:|---:|---:|
| HAS_INTEREST Expand self | 153 ms | 46 ms | **4.6 ms** | 4.6 ms |
| PC 子树 inclusive | 180 ms | 106 ms | **63 ms** | 63 ms |
| c10 总耗时（min/p50） | 490 / 496 ms | 407 / 430 ms | 345–366 / 359–376 ms | **277–300 / 285–298** |

- HAS_INTEREST 枚举从 2,611,476 行降到 0；对应 Expand 的 `wtSearchNear` 从 4,685 降到 1。
- 注：本机 CPU 频率会漂移（同一二进制不同时段可差 ~5%）；上表区间来自同一会话的多次采样，
  绝对值只在注明条件下引用。
- 10 行结果与 neo4j sf1（8688）逐行一致；`verify_ldbc_results.py` 21/21 MATCH。
- 剩余 277–300 ms 中 friend 侧约 215–245 ms：`(friend)<-[:HAS_CREATOR]-(post:Post)` 的
  80,607 条消息扫描 + Post 标签过滤（约 100 ms，按标签规模选 fetch/scan，见
  [expand-label-filter-optimization.md](../expand-label-filter-optimization.md) §5）+ LeftJoin /
  Aggregate / Distinct 等约 115 ms。
- 21,892 个 post 的 whole-object 构造（约 79 ms）已由
  [late-materialization-design.md](late-materialization-design.md) 的
  “Collected-list element demand” 规则去掉：`collect(post)` 的列表只被 `size()` 与
  Unwind 消费，元素停在拓扑阶段。

## 4. 测试

存储层：

- `WTGraphStoreTest.ScanEdgesExactBatchReportsQueryIndex`：索引映射、方向/标签/重复查询/
  缺失邻接、空批、callback 提前停止。

查询层（`tests/test_query_executor.cpp`）：

- `CorrelatedEndpointProbeBudgetFallsBackToExactSeek`：用
  `EUGRAPH_PROBE_MAX_EDGES_PER_SOURCE=1` / `EUGRAPH_PROBE_MAX_CACHED_EDGES=1` 强制两条
  回退分支，断言结果不变且 `probeFallbackCount() > 0`（证明回退真的被执行，而不是静默
  关掉探针）。
- `DestinationLabelFilterCostModelBothPathsAgree`：用 `labelFetchCount()` /
  `labelScanCount()` 断言 1 个邻居走 fetch、10 个邻居走 scan，两条路径返回相同的 1/10 行。
- `UserWrittenEndpointIdEqualityIsNotFoldedIntoExpand`：用户写的 `id(a)=id(b)` 不得被
  折进 Expand（EXPLAIN 里没有 `dst=bound`，`RETURN b` 仍然可见）。
- `ListComprehensionPatternPredicateAcrossWith`：结果 + EXPLAIN 断言 `dst=bound` 与
  `probe=set`；同一用例里 `head(posts).id` 反向验证列表被观察时守卫会退避。
- `ListComprehensionPatternPredicateCountsOnlyMatchingElements`、`PatternComprehension*`：
  端点约束与重数语义。

TCK（`tests/tck/features-eugraph/correlated-endpoint-and-size-list-regressions.feature`）：

- 14 个场景：相关 EXISTS/NOT EXISTS（绑定目的端、平行边重数、有向/无向自环、多关系
  类型）、用户 `id()` 等值不应改写、目标标签过滤（小/大邻居集两条路径）、`size([...])`
  列表元素拓扑化（正例 + 双 tag 同元素 + `head(posts).id` + 第二个 UNWIND 反向）。
- **期望值全部用本机 neo4j SF1（8688）在临时 `Tck*` 子图上跑同一 CREATE+查询得到**，
  逐行一致后才写入 feature；TCK 运行 14/14 场景、84/84 步骤通过。
- 定向 openCypher TCK `expressions/list` + `expressions/pattern`：235/235 场景、
  918/918 步骤通过。

回归：query_executor_tests 638/638、optimizer_tests 124/124。
