# c6 tag-anchored join reorder

Status: 2026-11-07. Narrow, guarded logical rewrite for the LDBC c6
tag-co-occurrence shape.

## Problem

c6 starts from the user's friends and expands every friend's posts, then checks
whether each post carries the known tag:

```text
knownTag (1) x person VLE (2,089)
  UNWIND friends
    Expand f <- HAS_CREATOR - post      238,882 rows
    Expand post - HAS_TAG -> t(knownTag) 243,446 rows
    Expand post - HAS_TAG -> tag         243,446 rows
```

On sf1 the known tag has only **863** posts (Tag label 16,080; Post label
1,003,605), so almost all of the 238k/243k intermediate rows are discarded.

## Rewrite

```text
knownTag -HAS_TAG-> post -HAS_CREATOR-> f
  CROSS friends(= collect(distinct friend), one row)
  FILTER f IN friends
  post -HAS_TAG-> tag
  FILTER tag != knownTag
  Aggregate(tag.name, count(post))
```

The one-row friends side is a cross product with the 863-row tag chain; no new
join operator is needed. `t` (the known-tag pattern variable) is replaced by the
`knownTag` scan variable in the outer filters.

## Guard

The rewrite runs in `LogicalOptimizer::optimize` before memoization and matches
all of:

- the exact c6 chain: `Filter(t.id=knownTagId) -> Expand(post->t, HAS_TAG, Tag)
  -> Expand(f->post, HAS_CREATOR, Post) -> Unwind -> Aggregate(collect distinct)
  -> Filter -> Cross(Project(knownTagId), person/VLE)`;
- single edge labels `HAS_TAG` / `HAS_CREATOR`, no edge property filter;
- Tag vertex count <= 1/8 of Post vertex count (from the query's label-count
  snapshot; falls back to catalog stats);
- a one-row friends aggregate (no group keys after the rewrite).

Any mismatch leaves the original plan untouched. Optional matches, multiple
patterns, path variables and writes are not matched.

## Stats plumbing

`LogicalOptimizer::optimize` now accepts an optional
`label_vertex_counts` map. `QueryExecutor` passes the same snapshot already used
by the physical planner's deferred-label gate; no catalog mutation and no CBO
cost change for other queries.

## Measurements (sf1)

Interleaved 3-round A/B against the D1 build (same data, same machine):

```text
D1 baseline: 2149 / 2124 / 2172 ms
reorder:       62 /   64 /   66 ms   (~33-35x vs D1)
```

- c6 result rows identical (10 rows, same order).
- All 21 LDBC read-query result hashes identical to the pre-change baseline.
- c5/c9/c2 unchanged within noise.
- 2026-11-08 最终版 SF1 全量 A/B（10 轮 + 3 warmup，eugraph 4GB WT，neo4j 2GB heap /
  2GB pagecache）：complex 合计 **8.58 s vs 3.23 s（2.65×）**；c6 **62.4 ms vs 435.2 ms
  （eugraph 快 6.97×）**；c14 1.80 s vs 280 ms；c5 2.79 s vs 1.10 s；c10 537 ms vs
  94 ms；c9 1.41 s vs 548 ms；c3 1.25 s vs 522 ms；short 合计 9.3 ms vs 7.9 ms。
  相对优化前 SF1 自比：complex **12.45 s → 8.58 s（-31%）**，c6 **2.47 s → 62 ms**。

## Files

- `src/query/optimizer/rules/logical/tag_anchored_join_reorder.{hpp,cpp}`
- `src/query/optimizer/logical_optimizer.cpp` (call + stats parameter)
- `src/query/executor/query_executor.cpp` (pass label counts)
- `tests/test_query_executor.cpp` (`C6TagAnchoredReorder`)

## Scope / follow-ups

This is intentionally a single-shape rewrite, not a general join-order
enumeration. The latter remains a Cascades gap; general reordering needs
cardinality-based alternatives and must not regress optional-match semantics.
c5's reverse semi-join was rejected: with 7.3 friends/forum x ~112 posts/person
x 72k forums the reverse direction is ~59M rows versus 832k today. A future
increment there is limited to pushing the friends id set into the creator
Expand (reduces output/Filter, not the 832k edge scans).
