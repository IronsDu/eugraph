# Property Demand / Late Materialization Refactor

Status: design accepted; implementation pending.

## Module ownership

- `optimizer/analysis/`: property demand and deferred-slot classification.
- `optimizer/rules/logical/`: late-materialization boundary rules.
- `optimizer/extraction/`: PEPlan/slot layout; consumes demand, never decides policy.
- `physical_plan/`: lowering of chosen physical operators only.
- `physical_plan/operator/`: execution semantics; no optimizer policy.

## Demand model

`PropertyDemand` records:

- `required_below_limit`: slots consumed by Filter/Sort/Aggregate.
- `output_only`: slots consumed only by the final Project.
- `deferred`: output-only slots that may be materialized after Limit.

The analysis pass is read-only. It must be reusable by the projection
extraction pass and must not live in `physical_planner.cpp`.

## Rules

A logical rule chooses the late-materialization boundary. The extraction pass
turns that decision into PEPlan/deferred slots. `LateMaterialize` is only the
physical lowering of that choice.

## Acceptance

- No policy decisions in `ExpandPhysicalOp`, `ShortestPathPhysicalOp`, or
  other execution operators.
- complex-9 output-only property loads happen after Limit.
- Existing unit/TCK tests pass; sf1 A/B is reported separately.
