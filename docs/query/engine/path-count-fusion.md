# Fixed-path count fusion (`PathCountPhysicalOp`)

> Status: implemented in the physical planner. Scope: fixed-length OUT
> expansions whose final vertex is only consumed by a non-DISTINCT `count`.

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

## 3. Planner recognition

`PhysicalPlanner` fuses only when all of these hold:

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

Everything else keeps the regular Expand/Filter/Aggregate plan. The fusion is
a physical optimization; it does not change logical semantics.

## 4. Correctness

- Relationship uniqueness: each previous edge whose source equals the current
  vertex is subtracted once; the prefix filter guarantees the previous edges
  are distinct.
- Self-loops: source and destination are both the current vertex; outdegree
  counts the self-loop once and the subtraction removes it when reused.
- Multi-edges: each physical edge contributes one to outdegree.
- Edge-label restriction: only matching outgoing entries are counted.
- `count(m)` vs `count(*)`: for a mandatory MATCH, `m` is a non-null vertex
  binding, so they are equivalent. The fusion currently keys on `count(m)`; no
  rewrite is required. OPTIONAL MATCH is not fused because `m` can be NULL.
- `count(m.firstName)` and any DISTINCT/grouped aggregate are not fused.

## 5. Verification

- `PathCountMatchesUnfusedAggregate`: compares fused `count(m)` with
  `count(m) AS c1, count(m) AS c2` (the second form cannot fuse) for 2-hop and
  3-hop graphs with self-loops.
- `RelationshipUniquenessRejectsReusedEdgeBeforeThirdHop`.
- `ExplainFinalHopCountFusion`: plan contains `PathCount`.
- sf0.1:

```text
MATCH (n:Person)-[]->()-[]->()-[]->(m) RETURN count(m)
old:  ~25-32s, 85,350,462 rows through Aggregate
new:  ~1.1-1.6s, count(m) = 85,350,462
```

The property variant remains on the normal path:

```text
count(m.firstName) = 8,819,691, ~26-28s
```

- `query_executor_tests` and TCK targeted features must pass; LDBC 21-query
  result verification is the external semantic check.
