# Optimizer layout

This directory follows the Columbia/Cascades split between the search
framework, read-only analyses, extraction/lowering, and rules.

```text
framework/     Cascades search core
  memo.*          Memo / Group / GroupExpr / Winner / copy-in/out
  task.*          EGraphTask, OGroupTask, OExprTask, OInputsTask, ApplyRuleTask
  opt_rule.*      OptRule pattern/condition/substitute + RuleSet
  cbo.*           Context, PhysProp, Cost, winner search helpers
  cost_model.*    operator cost functions
  log_prop.*      cardinality/schema derivation
  physical_expr.* physical-expression representation
  operator_eq/hash.* memo expression identity
  chosen_plan.hpp CBO-extracted physical plan tree

analysis/      Read-only passes that inspect the logical tree
  requirement_collector.*  VarRequirements for Cascades Enrichers
  materialization_req.hpp  materialization property model

extraction/    Demand-pull extraction decisions + expression lowering
  projection_extraction.*  PEPlan/requirements, slot allocation, alias/slot
                           propagation, column-index lowering

rules/
  logical/     transformation rules (FilterPushdown, EdgeUniquenessPushdown)
  physical/    logical -> physical implementation rules

logical_optimizer.*  pipeline entry: copy-in, task queue, CBO extract, RBO fallback
```

## Contracts

- Binder must only bind semantics and record references. It must not insert
  physical/plan-shape choices such as per-hop uniqueness filters or scan
  property lists.
- `analysis/` never mutates a plan.
- `rules/` receive a `GroupExpr` and return equivalent alternatives. A rule
  must not assume it is the only plan: CBO may keep the original expression.
- `extraction/` consumes the chosen/RBO logical plan and produces PE plans plus
  lowered slot references. It is not a rule layer.
- `requirement_collector` and `projection_extraction` are intentionally
  different: the former computes logical materialization demand for Cascades
  Enrichers; the latter computes physical flat-property demand/PE columns.
  They must not be merged without first unifying their output contracts.

## Adding a rule

1. Add a `OptRule` subclass under `rules/logical` or `rules/physical`.
2. Implement `pattern()`/`condition()`/`substitute()`; keep `substitute`
   purely functional (do not mutate the source expression).
3. Register it in `LogicalOptimizer::initRules()`, or pass it through
   `LogicalOptimizer::addRule()` for an extension experiment.
4. Add a plan-shape test and an execution/result test. Transformation rules
   must produce an equivalent plan; CBO chooses between alternatives.

`EUGRAPH_OPTIMIZER_FORCE_RBO=1` forces the RBO path for A/B diagnostics.
