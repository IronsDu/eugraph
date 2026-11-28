# Optimizer architecture and Columbia migration

Status: 2026-11-06. This document records the layer contract, the directory
layout, and the remaining Columbia gaps.

## 1. Layer contract

| Layer | Responsibility | Must not do |
|---|---|---|
| Binder | AST → logical operators; names/scopes/types; collect semantic references | choose filter placement, scan property lists, join order |
| analysis/ | read-only plan/expression analysis | mutate plans |
| normalization/ | pre-memo invariants/annotations (join conditions, batch strategy, join safety) | produce Cascades alternatives or use cost |
| rules/ | equivalent-plan alternatives (Columbia `RULE`) | mutate the source expression |
| post_cbo/ | one-way logical rewrites after CBO/lowering (no alternatives) | be registered as an OptRule |
| framework/ | memo, tasks, rule matching, CBO, cost/log properties | know query semantics |
| extraction/ | PE demand, slot allocation, expression lowering | be a transformation rule |
| physical planner | physical operators, access paths, PE dispatch | re-run logical optimization |

The binder now emits one global `__edge_unique` predicate. The optimizer rule
`EdgeUniquenessPushdownRule` recognises a fixed-length Expand chain and
produces the per-hop prefix alternative. It skips filters whose
`BoundFilterOp::uniqueness_scope` is `CorrelatedSubplan` (the right side of a
`PatternComprehensionApply`): there the global filter is cheaper and prefix
placement can disturb property-enforcer placement (complex-14 is the
regression case). The scope is logical-operator metadata, not Memo context, so
clone/equality/hash include it and the Memo remains context-free.

Property demand is modelled as a Cascades **required physical property**:
`LogicalOptimizer` derives `VarRequirements` from the logical plan, puts them
in the root `PhysProp`, and lets `OInputsTask` insert `VertexPropertyExtract` /
`EdgePropertyExtract` / `PathPropertyExtract` enforcers. `materializeChosen`
folds the enforcer's `enrich_output` into the topology source's
`label_prop_ids` / `edge_prop_ids`, which the physical planner then consumes.
The former projection-pushdown pass is removed. The RBO fallback derives
property reads from expressions during projection extraction; the CBO path uses
`PhysProp`/Enforcer.

## 2. Directory layout

See [src/query/optimizer/README.md](../../../src/query/optimizer/README.md).

- `framework/` — memo/tasks/rules/CBO.
- `analysis/` — `requirement_collector` + materialization model.
- `extraction/` — `projection_extraction` (PEPlan, slot and expression lowering).
- `normalization/` — `NormalizationPipeline`：`JoinConditionNormalization`、
  `JoinSafetyNormalization`、`AnnotateCorrelatedEndpointProbeNormalization`；
  这些是 memo 前的不变量/标注，不参与 alternative 搜索。
  **契约**：`JoinConditionNormalization` 只做语义规范化——把 Filter 下 Cross/Inner join 的
  等值谓词挂到 `join_conditions` 并保留 Filter 残差，**不选择 join 算法、不比较成本**；
  `join_conditions` 是 CBO 的事实输入，算法/tag 由 Impl 规则产出、由成本模型选 winner。
  **策略属性**：会改变执行策略但依赖运行期数据的标志（`build_left`、`batch_shortest_path`）
  是 `PhysicalExpr`/`ChosenPlan` 属性，由 Impl 规则产出、lowering 只转发；logical operator
  不再携带策略字段（P3 把 `batch_shortest_path` 从 `BoundVarLenExpandOp` 移出）。
- `rules/logical/` — `FilterPushdown`, `EdgeUniquenessPushdown`,
  `PathCountFusion`, `PatternComprehensionAnchorShare`；不包含按 LDBC
  label/关系名特判的规则。
- `rules/physical/` — implementation rules.
- `post_cbo/` — one-way logical rewrites after CBO/lowering, currently
  `pattern_comprehension_anchor_share` (PatternComprehensionApply has no CBO
  implementation rule).

## 3. Columbia mapping and gaps

Implemented:

- `Memo` / `Group` / `GroupExpr` / `Winner` / `Context`;
- rule matching (`OptRule::pattern/condition/substitute`), rule masks, task
  queue with exploration/optimization phases;
- `LogProp`, `CostModel`, physical properties, materialization Enrichers;
- logical→physical implementation rules;
- CBO chosen-plan extraction with RBO fallback.

Deliberately not yet implemented (framework supports adding them):

- general join-order enumeration/reordering rules；
- 通用 HashJoin 目前只覆盖可落到已有列的等值 join（Filter+CrossProduct），
  属性表达式/残差谓词仍回退；构建侧按标签基数估算选择，超过
  `EUGRAPH_HASH_JOIN_MAX_BUILD_ROWS` 回退，运行期有硬上限防 OOM；
  后续接入 CBO 代价选择；
- general projection/column-pruning rules (projection pushdown is currently a
  deterministic pass, not a memo rule);
- aggregate/distinct pushdown rules;
- full CBO for every full-object Enricher (the physical lowering falls back to
  RBO when a full-object enforcer wins).

Known framework debt:

1. Two requirement models (`VarRequirements` for logical Enrichers and
   `PlanRequirements` for PE) traverse expressions separately. They have
   different output contracts, so they are kept in `analysis/` and
   `extraction/` rather than merged; sharing the AST walk is a future cleanup.
2. `projection_extraction` is still a large unit (PE demand + PEPlan + slot
   lowering). The directory now separates it from rules/analysis, but a future
   split into `property_demand` and `slot_lowering` is desirable.
3. Mandatory normalizations are now separated into `normalization/` rather than
   being hidden as rules. `OptRule::substitute` still cannot rewrite nested
   child-group metadata; extending Memo with a child-group normalization API
   would let `JoinConditionNormalization` become a true rule if desired.
4. RuleSet registration is explicit in `initRules`; `LogicalOptimizer::addRule`
   is the extension hook for experiments (one optimizer instance per query).

## 4. Verification

- `query_executor_tests`: focused uniqueness/property tests pass;
  full suite must be rerun after the layout/rule change.
- TCK `Match7`/`Match8` and LDBC result/benchmark checks are required before
  merging.
