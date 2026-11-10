# Pattern comprehension anchor sharing

Status: implemented (SF1 complex-14: 1,800.9 ms -> 1,101.7 ms, scanRows −49%;
the endpoint-id hoisting and the prefetch fix below account for most of it).
Remaining work: branch expansions (see the end of this document).

## Problem

`complex-14` runs two list comprehensions over `rels_in_path`, each holding a
pattern comprehension that differs only in the label of the destination of
`REPLY_OF`:

```text
(a:Person)<-[:HAS_CREATOR]-(:Comment)-[:REPLY_OF]->(:Post)    -[:HAS_CREATOR]->(b)  weight 1.0
(a:Person)<-[:HAS_CREATOR]-(:Comment)-[:REPLY_OF]->(:Comment) -[:HAS_CREATOR]->(b)  weight 0.5
```

The binder hoists each comprehension into its own
`BoundPatternComprehensionApplyOp`, so the `a <-[:HAS_CREATOR]- comment`
expansion — the correlated traversal that dominates the query — was executed
once per comprehension (2 x 4,131,230 scanned rows at SF1).

## Plan

```text
PatternComprehensionApply (2 outputs)
  Aggregate(keys=0, collect(list1), collect(list2))
    PatternComprehensionFork
      left:    Unwind(rels_in_path) + hoisted endpoint ids
      anchor:  a <-[:HAS_CREATOR]- comment     executed ONCE per outer batch
      branch0: REPLY_OF :Post    -> b -> 1.0
      branch1: REPLY_OF :Comment -> b -> 0.5
```

The anchor and the branches are plan children; the anchor's rows are
materialised once and replayed into both branches. This is an explicit plan
DAG, not an operator-local cache: `CorrelatedSourcePhysicalOp::setChunks()`
replays the anchor's chunks without re-packing values.

## Logical operator

`BoundPatternComprehensionForkOp` (`bound_pattern_comprehension_fork_op.hpp`):
`left`, `anchor`, `branches[]`, `correlation`, `outputs`, batch fields.
Child order in the memo / extraction is `left, anchor, branches...`
(`2 + N`); `getChildCount`/`copyIn`/`copyOut`/clone/equality/hash and the
log-prop derivation follow that order.

## Rewrite

`optimizer::rewritePatternComprehensionAnchorShare()` in
`src/query/optimizer/rules/logical/pattern_comprehension_anchor_share.cpp`,
called from `PhysicalPlanner::planBound` **after** slot allocation,
materialisation requirements and column-index lowering.

It is not a Cascades `OptRule`: a plan containing a `PatternComprehensionApply`
can never have a CBO winner (there is no implementation rule for it), so the
fork would never be selected by the CBO. Running after the analysis passes also
means those passes keep seeing the same tree they always saw.

Recognition (all guards fail closed — the original plan is kept):

1. two **stacked** applies whose left is the other apply, identical outer
   correlation, one output each, no existence shortcut, same batching;
2. each apply's right sub-plan is `Aggregate(keys=0)` over the *inner*
   comprehension apply; both inner applies have the same correlation;
3. the two comprehensions bind their own copies of the pattern variables
   (`r`, the anonymous comment/target variables, the batch key), so the second
   comprehension's generated names/slots/`ScopeId`s are **paired positionally**
   onto the first (Expand `src`/`dst`/`edge`, correlated-source columns, unwind
   variable, expression column refs);
4. after renaming, the driving sub-plans must be structurally identical, and the
   two right sub-plans must share a **suffix** that is structurally identical
   (payload *and* children — `planTreesEqual`, since the memo's content
   comparison deliberately ignores children);
5. the shared suffix must contain the correlated traversal itself (an Expand /
   VarLenExpand), so a fork never replaces a trivial pipeline.

The merged plan keeps the two aggregates' arguments verbatim: they reference the
per-r comprehension output (possibly wrapped in the list-comprehension's
`reduce` expression), and the fork appends exactly those slots.

### Hoisting the endpoint ids

The branch predicates compare the endpoints' *property* ids against
`startNode(r).id` / `endNode(r).id`. `startNode` synthesises a bare vertex, so
the `.id` access is a **dynamic** property lookup: one store fetch per evaluated
row (~77k rows per branch, ~307k lookups measured with an instrumented build).

Both are constant for the whole inner execution, so the rewrite also:

- adds a projection on the fork's **left** pipeline that passes the correlated
  columns through and computes the two endpoint ids once;
- extends the anchor's correlated source with the two columns (the fork injects
  them by name; the planner falls back to name lookup for non-correlation
  columns);
- rewrites both branch predicates to compare the new columns.

The projection is never placed **inside** the anchor: that defeats the
`CorrelatedVertexProbe` rewrite and measured 4.0–4.2 s (worse than the 2.2 s
baseline). All-or-nothing: the plan is only touched once every piece has been
built successfully.

## Related fixes this work depended on

- `perf(compute): read coalesced vertex properties once per distinct vertex` —
  tried and **reverted**: deduping `LoadVertexPropCoalesce` by distinct vertex
  saved only ~6% on complex-14 but made complex-3 (1,256 -> 1,865 ms) and
  complex-9 (1,430 -> 2,272 ms) much slower (the per-row fan-out copy of the
  cached property vector dominates), located by an A/B on the same dataset.
  Do not re-land it without fixing that copy path.
- `perf(compute): prefetch only the columns an expression references` — the
  prefetch pass scanned **every** graph column of the chunk, so a Filter whose
  predicate reads one property also fetched the anonymous pattern vertices of
  every row (~180k vertex fetches per query, 822 ms). This was the single
  largest remaining cost before the fix.

## Verification (SF1, `person1Id=933`, `person2Id=1129`)

| metric | before | after |
|---|---:|---:|
| wall time | ~1,800 ms (pre-optimisation doc) | **1,101.7 ms** |
| total scanRows | 8,470,381 | 4,329,259 (−48.9%) |
| anchor Expand | 2 x 4,131,230 rows | 1 x 4,131,230 rows |
| branch `b.id` fetches | 152,440 | 152,440 (dedup reverted, see above) |
| endpoint dynamic lookups | ~307,000 | 0 |
| branch predicate store reads | ~180,000 | 0 |
| result | 12 rows | 12 rows, identical `pathWeight` |

Every number above comes from same-machine / same-data runs; the per-change
figures are interleaved A/B (same binary where a switch exists, otherwise an
interleaved rebuild).

Regression tests:
`QueryExecutorTest.PatternComprehensionAnchorShareIsUsedForSharedPrefix` (fork is
built, both weights correct) and
`...KeepsPlanWhenPrefixDiffers` (fail closed when there is no shared traversal).

## Limitations

- The recognised shape is the list-comprehension-wrapped pair produced by
  `complex-14`; a plain pair of sibling comprehensions is not rewritten yet.
- Only the *shared* part is amortised: the branch tails (label-filtered
  `REPLY_OF` / `HAS_CREATOR` expands) still run once per branch.

## Next: branch expansions (~380 ms of the current ~1.0 s)

Current profile after all of the above:

| part | self ms |
|---|---:|
| anchor Expand (4.13M rows) | ~388 |
| branch Expand `REPLY_OF` + `HAS_CREATOR` | 134 + 58 and 132 + 48 |
| branch PE (`b.id`) | 54 + 48 |
| shortestPath | ~30 |

Two directions, in increasing risk:

1. **Widen the shared prefix by one hop.** The two branches differ only in the
   destination label of `REPLY_OF`. If the shared segment ends *after*
   `REPLY_OF` with the destination label set widened to `{Post, Comment}`, both
   branches would share that expand too; the branch then only needs to test
   which label the destination carries. That needs either a cheap label test in
   a projection (a `LabelPredicate` expression, or a coalesced
   `labelId → weight` column) or a PE column carrying the destination's
   labels. Guard it like the anchor share: same edge label, same direction,
   identical downstream shape.
2. **Semi-join the `HAS_CREATOR -> b` step.** `b` is constrained to the paths'
   endpoint persons (a set of ~8 vertices). A vertex-set semi-join before the
   `HAS_CREATOR` expand (the machinery already exists for `Expand`'s
   `allowed_dst_values`) would cut that expansion to matching rows only.

Both need the same acceptance bar used here: SF1 wall time + scanRows, 12 rows
with identical `pathWeight`, `query_executor_tests`, and TCK.
