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

## c5 — 已实施（264.1 ms）

不做 BatchExpand：把 `LeftJoin` 的 correlated 右子计划改为**每 512 个 left row 执行一次**
（`CorrelatedSource` 多行 + 按 forum/friends 匹配回左行 + NULL 补全），消除 8,814 次子计划重启。
c5 514 ms → **264.1 ms**，20 行结果与原路径逐值一致；全量 ctest 1304/1304。

注意：批大小必须 < 1024；1,024/2,048 会触发 1,024 行 chunk 边界上的丢值（待查）。
右子计划输出含 `PATH` 时（`OPTIONAL MATCH p = ...`）回退逐行执行，避免 named path 回归。
剩余为 89k posts/creator 的 per-edge 成本，下一步是 OUT/IN 邻接存储或进一步去相关。

## State

Branch `perf/ldbc-c5-c14-main`；c9 161.7 ms、c5 264.1 ms、c14 295 ms。
Tests: `query_executor_tests` 584/584；全量 ctest 1304/1304（含 TCK）。
storage 后续方向（label/hot-property 共置、creationDate 索引、IN cache）见
[adjacency-cache-and-vertex-data-research.md](../storage/adjacency-cache-and-vertex-data-research.md)
（该文档在 `feat/vertex-data-adjacency` 分支，尚未合并到 main）。
