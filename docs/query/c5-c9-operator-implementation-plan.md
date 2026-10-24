# c5/c9 Operator Implementation Plan

## c9 — 已实施（161.7 ms）

两个物化边界，均由物理计划改写实现，不修改 binder / 存储格式：

1. **LIMIT 边界**：`LIMIT(Project(...))` → `Project(LateMaterialize(LIMIT(...)))`；
   只被 RETURN 使用、不被 Sort/Filter 使用的列延后到 LIMIT 之后（20 行）。
2. **Filter 边界**：Sort 的 child 是 Filter 时，只被 ORDER BY 使用、Filter 谓词与
   Filter 子树都不用的列（c9 的 `message.id`）延后到 Filter 之后（7,706 行）。
3. `LateMaterializePhysicalOp` 复用 PE 的逐列物化核心，自有完整 schema/slot layout。
4. Sort 支持 LIMIT 下推（Top-N），避免全量排序；结果列/值与原路径逐值一致。

实测（sf0.1，5 轮 min）：原路径 415.4 ms → 245 ms（LIMIT LM）→ **161.7 ms**（+Filter LM）。
剩余成本：`creationDate` 对 53,192 行的 label + per-label vprop 点查（约 98 ms）、
Expand 53k 边（约 45 ms）；详见 [ldbc-c5-c9-plan-analysis.md](ldbc-c5-c9-plan-analysis.md)。

## c5 — 未实施，原 BatchExpand 方案需修正

当前计划中 `LeftJoin` 的右子计划由 `CorrelatedSource` 对每个 left row 执行一次（8,814 次），
每次只有 1 个 forum；`BatchExpandPhysicalOp` 的 >2048 行批量路径不会触发。
需要先做**右子计划去相关 / join reorder**（按 forum 只执行一次并 hash join 回左侧），
再叠加批量 Expand；保留 OPTIONAL 零计数语义。Target 514ms → <=300ms。

## State

Branch `perf/ldbc-c5-c9`；c9 161.7 ms，c5 未开始。
Tests: `query_executor_tests` 584/584；全量 ctest 见提交说明。
storage 后续方向（label/hot-property 共置、creationDate 索引、IN cache）见
[adjacency-cache-and-vertex-data-research.md](../storage/adjacency-cache-and-vertex-data-research.md)
（该文档在 `feat/vertex-data-adjacency` 分支，尚未合并到 main）。
