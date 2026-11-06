# Fixed-path count fusion (`PathCountPhysicalOp`)

> Status: implemented as a logical rewrite plus a CBO implementation
> alternative. Scope: fixed-length OUT expansions whose final vertex is only
> consumed by a non-DISTINCT `count`.

## 1. Motivation

For a three-hop walk with no final label/property constraint:

```cypher
MATCH (n:Person)-[]->()-[]->()-[]->(m)
RETURN count(m)
```

the plan materialises one row per third-hop edge. On sf0.1 this is about
85.3M rows even though `count(m)` only needs a scalar. The storage layer scans
only ~250k edges (neighbour cache), so the dominant cost is row construction
and consumption, not edge lookup.

The final hop can be counted without materialising its edges. For each prefix
path

```text
n -e0-> a -e1-> b
```

the number of valid `e2` successors is:

```text
outdegree(b)
  - (source(e0) == b ? 1 : 0)
  - (source(e1) == b ? 1 : 0)
```

because relationship uniqueness forbids reusing `e0` or `e1`. `source(e0)` and
`source(e1)` are already present in the prefix row. This formula generalises to
N hops: subtract every previous edge whose source is the current vertex.

## 2. Operator

`PathCountPhysicalOp`:

- input: the prefix plan after N-1 hops (including its uniqueness prefix
  filters);
- `source_column`: the current vertex column;
- `previous_edge_columns`: the N-1 previous EDGE/EDGE_KEY columns;
- optional `edge_labels`: the final Expand's edge-label restriction.

For each prefix row it adds the number of valid final edges to a running
counter, then emits one INT64 row. Degrees are memoised per `VertexId` for the
duration of the operator.

No final `m`/edge columns are materialised.

## 3. Optimizer recognition

`PathCountFusionRule` rewrites the logical aggregate into `BoundPathCountOp`
when all of these hold:

- aggregate has no group keys and exactly one non-DISTINCT, non-internal
  `count`;
- child is the top relationship-uniqueness Filter over the final Expand;
- final Expand is `LEFT_TO_RIGHT`, and its destination/edge are not bound;
- final destination has no label/property pushdown (`dst_label_ids`,
  `dst_label_prop_ids` empty) and final edge properties are not requested;
- the aggregate argument is the final destination variable (not
  `count(DISTINCT ...)` / property / expression);
- for N>2, the final Expand child has the prefix `__edge_unique` filter for
  the first N-1 edges;
- the final edge-label set, if any, is supported by filtering the counted
  adjacency.

The rewrite is a Memo candidate: `ImplPathCountRule` produces the
`PhysicalOpTag::PathCount` alternative, so CBO compares it with the regular
Expand/Filter/Aggregate alternative. `PathCountPhysicalOp` only implements the
chosen logical operator; there is no Aggregate-branch special case in the
physical planner.

`count(*)`, `count(1)` and other non-NULL constant arguments are accepted in
addition to `count(m)`. `count(NULL)` and `count(DISTINCT ...)` are not.
Everything else keeps the regular Expand/Filter/Aggregate plan.

## 4. Correctness

- Relationship uniqueness: each previous edge whose source equals the current
  vertex is subtracted once; the prefix filter guarantees the previous edges
  are distinct.
- Self-loops: source and destination are both the current vertex; outdegree
  counts the self-loop once and the subtraction removes it when reused.
- Multi-edges: each physical edge contributes one to outdegree.
- Edge-label restriction: only matching outgoing entries are counted.
- `count(m)` vs `count(*)`: for a mandatory MATCH, `m` is a non-null vertex
  binding, so they are equivalent. Both forms (and `count(1)`) are recognized;
  no textual rewrite is required. OPTIONAL MATCH is not fused because `m` can
  be NULL.
- `count(m.firstName)` and any DISTINCT/grouped aggregate are not fused.

## 5. Memory bounds

`PathCountPhysicalOp` is streaming with respect to the prefix and never
materialises the final rows. Its only growing container is the per-operator
degree cache (`VertexId -> outdegree`).

That cache is bounded:

```text
EUGRAPH_PATH_COUNT_CACHE_MAX_ENTRIES
default: 1,048,576 entries
0: disable caching
```

When the bound is reached, new source vertices are counted without inserting
into the cache; existing entries remain usable and results are unchanged.
The cache is per operator instance and is released when the operator finishes.
Multiple PathCount operators or concurrent queries therefore multiply the
bound, so a server-wide memory budget should be selected with that in mind.

Adjacency counting uses the existing streaming `scanEdges` batches (4096
entries per batch); it does not retain a complete supernode adjacency vector.
The current cache bound protects the large-graph case where the prefix reaches
millions of distinct vertices, while leaving normal LDBC-sized working sets
(about 327k distinct vertices) fully cached.

## 6. Verification

- `PathCountMatchesUnfusedAggregate`: compares fused `count(m)`, `count(*)`
  and `count(1)` with their two-aggregate forms (which cannot fuse) for 2-hop
  and 3-hop graphs with self-loops.
- `RelationshipUniquenessRejectsReusedEdgeBeforeThirdHop`.
- `ExplainFinalHopCountFusion`: plan contains `PathCount`.
- sf0.1:

```text
MATCH (n:Person)-[]->()-[]->()-[]->(m) RETURN count(m)
old:  ~25-32s, 85,350,462 rows through Aggregate
new:  ~0.9-1.6s, count(m)/count(*)/count(1) = 85,350,462
```

The property variant remains on the normal path:

```text
count(m.firstName) = 8,819,691, ~26-28s
```

- `query_executor_tests` and TCK targeted features must pass; LDBC 21-query
  result verification is the external semantic check.
