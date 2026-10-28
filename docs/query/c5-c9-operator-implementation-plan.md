# c5/c9 Operator Implementation Plan

## c9 — 已实施（最新 177.1 ms；旧 161.7 ms 作废）

正确的物化边界：

1. **LIMIT 边界**：`LIMIT(Project(...))` → `Project(LateMaterialize(LIMIT(...)))`；
   只被 RETURN 使用、不被 Sort/Filter 使用的列延后到 LIMIT 之后（20 行）。
2. Sort 支持 LIMIT 下推（Top-N），避免全量排序。
3. **Filter 边界 LM 已移除**：它把 Sort 需要的 `message.id` 延迟到 Filter 之后，
   并在 Sort 下方放 LateMaterialize；没有 LIMIT 时 sort key 未物化，Top-N 错误。
   修复后与 neo4j 逐行一致。

实测（sf0.1，5 轮 min）：优化前正确基线 422 ms；LIMIT LM+Top-N 约 245 ms；
storage 分支 + 共享 cursor + 批量读取后 **177.1 ms**。c9 行结果与 neo4j 一致。
详见 [ldbc-c5-c9-plan-analysis.md](ldbc-c5-c9-plan-analysis.md)。

## c5 — 已实施（最新 246.5 ms）

`LeftJoin` 的 correlated 右子计划每 512 个 left row 执行一次
（`CorrelatedSource` 多行 + 按 forum/friends 匹配回左行 + NULL 补全），消除 8,814 次子计划重启。
`scanEdgesBatch` 每 4,096 个 source 复用一个 cursor，消除 c5 实测 98k 次 per-source cursor。
c5 514 ms → 264.1 ms（批处理）→ 234 ms（共享 cursor）→ **246.5 ms**（最新全量，波动范围内），
20 行与 neo4j 逐行一致。

注意：批大小必须 < 1024；1,024/2,048 会触发 1,024 行 chunk 边界上的丢值（待查）。
右子计划输出含 `PATH` 时（`OPTIONAL MATCH p = ...`）回退逐行执行，避免 named path 回归。

## State

Branch `feat/storage-vertex-data`；最终同轮 A/B：c5 234.0 ms、c9 187.2 ms、
c14 291.4 ms，行均与 neo4j 一致；全量 ctest 1304/1304（含 TCK）。
存储与计划决策见 [kv-encoding.md](../storage/kv-encoding.md) 与
[ldbc-snb-sf0.1-comparison.md](../benchmark/ldbc-snb-sf0.1-comparison.md)。
