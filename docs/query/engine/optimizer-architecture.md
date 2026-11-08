# Optimizer architecture and Columbia migration

Status: 2026-11-06. This document records the layer contract, the directory
layout, and the remaining Columbia gaps.

## 1. Layer contract

| Layer | Responsibility | Must not do |
|---|---|---|
| Binder | AST → logical operators; names/scopes/types; collect semantic references | choose filter placement, scan property lists, join order |
| analysis/ | read-only plan/expression analysis | mutate plans |
| rules/ | equivalent-plan alternatives (Columbia `RULE`) | mutate the source expression |
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
- `rules/logical/` — `FilterPushdown`, `EdgeUniquenessPushdown`, plus the
  guarded `TagAnchoredJoinReorder` pre-pass (c6 shape only).
- `rules/physical/` — implementation rules.

## 3. Columbia mapping and gaps

Implemented:

- `Memo` / `Group` / `GroupExpr` / `Winner` / `Context`;
- rule matching (`OptRule::pattern/condition/substitute`), rule masks, task
  queue with exploration/optimization phases;
- `LogProp`, `CostModel`, physical properties, materialization Enrichers;
- logical→physical implementation rules;
- CBO chosen-plan extraction with RBO fallback.

Deliberately not yet implemented (framework supports adding them):

- general join-order enumeration/reordering rules (the c6 tag-anchored
  pre-pass is a single guarded shape, not a general reorder);
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
3. Rule subsumption masks are not exposed. A transformation that is always
   better must currently rely on CBO cost; if the cost model cannot see the
   benefit, the original expression can win. `EdgeUniquenessPushdownRule`
   produces a valid alternative and is selected when its estimated cost is
   lower; a future `subsumes` API would let mandatory normalizations mark the
   source.
4. RuleSet registration is explicit in `initRules`; `LogicalOptimizer::addRule`
   is the extension hook for experiments (one optimizer instance per query).

## 4. Verification

- `query_executor_tests`: focused uniqueness/property tests pass;
  full suite must be rerun after the layout/rule change.
- TCK `Match7`/`Match8` and LDBC result/benchmark checks are required before
  merging.
