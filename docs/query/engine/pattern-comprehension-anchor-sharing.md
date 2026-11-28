# Pattern comprehension anchor sharing（已删除）

Status: **removed 2026-11-28 (P4)**. 本文原记录 LDBC complex-14 的 shared-anchor fork
设计（`PatternComprehensionFork` + post-CBO 重写）。重写与 `optimizer/post_cbo/` 已删除，
本文保留为历史记录，供既有 benchmark 文档的链接与结论回溯。

## 为什么删

该重写是 post-CBO 的唯一策略：把两个 stacked `PatternComprehensionApply` 合成
`Apply(Aggregate(Fork(left, anchor, branches)))`，让 `a <-[:HAS_CREATOR]- comment` 只执行
一次。设计时（2026-11-11）它是 c14 的主要成本：SF1 上每个 comprehension 各自 4,131,230
scanned rows。

后来 `CorrelatedVertexProbe`（B3 的 lowering 特判）把每个 comprehension 的 anchor 展开
变成按行精确探针，anchor 不再是扫描热点。P4 用同二进制 A/B（`EUGRAPH_ANCHOR_SHARE_OFF=1`，
c14，person1=933/person2=1129，warm 1 + 3 iters，min）：

```text
anchor share on  (fork)   781.1 ms   12 行
anchor share off (2 applies) 784.4 ms  12 行
```

差异为噪声，且 fork 计划与 off 计划里 `CorrelatedVertexProbe` 都出现（每分支一个），
说明 probe 不依赖 fork。保留一个 997 行的 post-CBO 重写、一个专用 logical/physical
操作符和一个物化边界后的计划改写阶段，收益为 0，因此删除：

- `src/query/optimizer/post_cbo/*`（`applyPostCboRewrites` + `rewritePatternComprehensionAnchorShare`）；
- physical planner 的两处调用（`planBound` / `planChosen`）；
- 本设计文档正文（本页为删除记录）。

`PatternComprehensionApply` 现在按普通 CBO 算子执行（Impl 规则早已存在），不再有
post-CBO 改写阶段。B3（`CorrelatedVertexProbe` 的 CBO 候选化）的阻塞结论随之更正：
探针形状不依赖 fork，可以独立重做；细节见
[physical-planner-residue-audit.md](physical-planner-residue-audit.md) 的 B3 / D2。

## 历史内容索引

原文的 Plan/逻辑算子/执行模型/测量章节可从 git 历史取回（本文件删除前的最后一个版本）。
相关：`docs/benchmark/ldbc-plan-quality-analysis.md`、`docs/benchmark/ldbc-snb-sf0.1-comparison.md`
（其中 c14 的 fork 结论是 2026-11 的记录，P4 之后不再成立）。
