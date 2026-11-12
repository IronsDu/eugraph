# Late Materialization Design (sf1 complex-9 / c2 / c5)

## Goal

Move output-only property loads from the pre-LIMIT pipeline to the surviving
rows after LIMIT, without duplicating policy decisions in the physical planner.

## Ownership

- `optimizer/analysis`: `PropertyDemand` (read-only classification).
- `optimizer/extraction`: consumes demand and builds `PEPlans`; owns slot layout.
- `optimizer/rules/logical`: chooses the late-materialization boundary.
- `physical_plan`: lowers the chosen plan only.
- `physical_plan/operator`: execution semantics only.

## Data flow

```
BoundLogicalPlan
  -> PropertyDemand analysis
  -> PEPlans split:
       pre_limit_plans   (required_below_limit)
       deferred_plans    (output_only)
  -> physical planner lowers pre_limit_plans normally
  -> Limit -> LateMaterialize(deferred_plans)
```

## Invariants

- `output_only` slots never appear in pre-LIMIT PEPlans.
- A slot may be in both sets only if an operator below LIMIT truly consumes it;
  then it is not output-only.
- Extraction must not re-infer demand from `Project` shape.
- Physical planner must not call `computeDeferredSlots`.
- Alias/canonical slot resolution happens in extraction, not operators.

## Whole-object vs flat-column deferral

The split above partitions **flat PE columns** (props / labels / type). A whole
`VertexValue`/`EdgeValue` construct is a different decision: it is controlled by
`need_whole_vertex/edge`, not by the LIMIT split. The demand collector therefore
must not raise whole-object demand for identity-only uses:

- an intermediate `WITH n` / `WITH n AS m` forward,
- an Aggregate group key that is a bare entity reference,
- `id(n)` / `elementId(n)` / `element_id(n)` (2026-11-12): the id lives in the
  `VERTEX_REF` / `EDGE_KEY` column and `function::scalar::idBatchFn` has a
  dedicated ref batch path, so the call is an identity consumer like the two
  cases above. Before the rule, the function-call walk descended into the
  argument and marked the entity `need_whole_vertex`, so
  `MATCH (n:Person) RETURN count(id(n))` on SF1 built 9,892 `VertexValue`s
  (2 extra storage reads each) and took **37.7 ms**; the same query now takes
  **1.6 ms** with no `ProjectionExtract` in the plan. Regression:
  `QueryExecutorTest.IdFunctionDoesNotConstructObject`, with
  `PropertiesFunctionStillConstructsObject` as the guard that
  `properties()/keys()` still materialise the object.

Only true object consumers (statement output that returns the entity, SET /
REMOVE / DELETE / MERGE, dynamic property access, labels()/type(), ...) raise it.
Statement-output entity slots are re-marked after the demand walk by
`collectTerminalEntitySlots`, so `RETURN n` and `RETURN a, count(*)` keep
building a VertexValue.

This is what removes the c5 538k-row `forum<ctor-vertex>`: c5 only groups by
forum and reads `title` after LIMIT. The construct is not replaced by a flat
column at the same boundary — it disappears entirely from the pre-LIMIT PE.

Representation metadata must follow the real column: a forwarded entity that is
still a `VERTEX_REF` must not be declared `VERTEX` by Project/Aggregate, or the
next operator stores the ref into a VertexValue column and silently loses it.

## API changes

- `projection_extraction` accepts `const PropertyDemand&`.
- `buildExtractionInfo` produces both pre-limit and deferred PEPlans.
- `physical_planner` receives the two PEPlans and lowers them; no local analysis.

## Acceptance

- complex-9: output property columns loaded for <=20 rows, not 916k.
- complex-2: output-only content/imageFile loaded after LIMIT.
- complex-5: the 538k-row PE no longer constructs forum (sf1 interleaved A/B,
  3 alternating baseline/new cycles: 3.72s → 2.82s, -24%). The forum title
  coalesce still loads at the Filter boundary because the Project above
  suppresses filter deferral; relaxing that suppression safely is a follow-up
  (the TCK `WITH other WHERE r IS NULL` guard depends on it).
- complex-9 has identical PROFILE dbHits (916,576 creationDate reads); an
  interleaved A/B is within ±1.5%. c2 is unchanged (~34ms).
- Results equal neo4j; TCK passes; sf1 A/B before/after.

## Implementation status (2026-11-07)

- Whole-object identity rules (intermediate `WITH` / Aggregate group keys) are
  implemented in extraction, together with the representation metadata fix in
  `planProjectOperator` / Aggregate. c5 is verified; c9/c2 are unchanged.
- The extraction-owned LIMIT split is still partially pending: demand is
  currently derived from the plan root (a `Limit` for LIMIT queries), so
  `split_pe_plans.deferred` can be empty and the physical planner still falls
  back to `computeDeferredSlots`. This does not affect the whole-object change;
  it is tracked as separate ownership work.

## Collected-list element demand (2026-11-11)

`collect(x)` used to raise whole-object demand on `x` unconditionally. For

```cypher
WITH friend, city, collect(post) AS posts, person
RETURN size([p IN posts WHERE (p)-[:HAS_TAG]->()<-[:HAS_INTEREST]-(person)])
```

the list is only counted and its elements are only used as pattern endpoints, yet
c10/SF1 built 21,892 posts into full `VertexValue`s (labels + all properties,
three storage reads each, ~65k B-tree reads / ~79 ms).

The binder now marks a lowered list comprehension whose result is a direct
`size()` argument (`BoundPatternComprehensionApplyOp::size_only_output`). Before
`collectPlanRequirements`, the physical planner verifies the whole bound plan:

- the collected entity appears in exactly one expression (the `collect` argument),
- the list variable appears in exactly one non-`size` expression (the `UNWIND`);
  any number of direct `size(list)` calls is allowed,
- the loop variable appears in exactly one expression (the comprehension's own
  `collect(loop var)`),
- neither the entity nor the loop variable is a path element,
- the `collect` is not DISTINCT.

Only then is `need_whole_vertex` cleared, so the PE passes the topology column
(`VERTEX_REF` / `EDGE_KEY`) into the list. `collect` stores the values verbatim,
`UNWIND` and the pattern endpoints read ids, and `size()` counts. The guard is
conservative: observing the list (`head(posts).id`), reading a property in the
comprehension body, returning the entity, using it in a path, or a DISTINCT
collect all keep the constructed object.

c10/SF1: 345–366 ms → **277–300 ms** (clean binary min 292 / p50 298); 10 rows unchanged.
Correctness coverage: `QueryExecutorTest.ListComprehensionPatternPredicateAcrossWith`
(positive + `head(posts).id` negative), and the TCK feature
`tests/tck/features-eugraph/correlated-endpoint-and-size-list-regressions.feature`
(14 scenarios whose expected rows were produced by neo4j SF1; includes two tags on
one element, `head`, and a second UNWIND over the collected list). See
[correlated-endpoint-set-probe.md](correlated-endpoint-set-probe.md) for the
companion endpoint work.
