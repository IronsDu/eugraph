# PatternComprehensionApply batching

Status: 2026-11-07. First implementation of correlated sub-plan batching for
`PatternComprehensionApply`.

## 1. Motivation

`PatternComprehensionApply` used to execute its right (correlated) sub-plan
once per left row. Each execution reconstructed a `CorrelatedSource` row,
a generator chain, and the final `Aggregate(collect)` group. On LDBC c10 this
was ~41 ms for 3.7k posts, with a 583-row correlated Expand alone costing
~20 ms. The work was dominated by per-row pipeline setup rather than edge
reads.

## 2. Design

The binder now injects an internal batch key into the right sub-plan:

- `bindPatternComprehension` allocates `__pc_batch_<id>` (INT64) and passes it
  to `bindExistsSubPlan` as an extra `CorrelatedSource` variable.
- The right `Project` forwards that column; the right `Aggregate(collect)`
  gains it as the **first group key**, so it emits one row per left row:
  `[batch_id, list0, list1, ...]`.
- `BoundPatternComprehensionApplyOp` carries `batch_enabled`, `batch_slot_id`
  and `batch_var`; the Memo clone preserves them.

At execution:

- `PatternComprehensionApplyPhysicalOp` splits a left chunk into batches of
  256 rows, appends the batch index to each correlated row, and calls
  `CorrelatedSource::setRows` once per batch.
- It drains the right sub-plan once per batch, reads the batch key, and writes
  each list column into `ColumnBuffer::list_data[left_row]`.
- Rows with no right-side group (no pattern match) keep the pre-initialized
  empty list, preserving Cypher semantics.
- Existence-only comprehensions stay on the per-row path: they stop at the
  first match, so batching would change their cost model. The per-row path
  still offsets list columns by the batch key column.
- Fallback: if the right schema has no batch key, the operator uses the
  original per-row path with `list_column_start = 0`.

## 3. Invariants / risks

- Left row order and count are unchanged; one list per output per left row.
- Multiple PC outputs are supported: list columns follow the batch key.
- Nested PCs each get their own batch key on their own `CorrelatedSource`.
- Empty matches must not invent a group row: the Aggregate has no row for that
  batch id, so the pre-initialized empty list is emitted.
- The batch key is internal: it is not exposed in the Apply output schema and
  does not enter user-visible slot resolution.
- Memo clone must copy the three batch fields; otherwise CBO falls back to
  per-row (observed as empty/incorrect lists before the clone fix).
- Batch execution is deliberately disabled for existence-only plans and when
  the right schema has no batch column.

## 4. Measurements

Same-machine interleaved A/B, sf0.1, two data copies, 8/4 threads, 1 GB WT
cache:

| query | baseline min | batch min | change |
|---|---:|---:|---:|
| complex-10 | 41.2 ms | 20.4 ms | **2.02× faster** |
| complex-14 | 84.0 ms | 85.1 ms | no change (actual expand work dominates) |
| complex-5 (control) | 219.0 ms | 222.4 ms | noise |

c10's result hash is unchanged. c14 stays on the batch path but gains little:
its cost is the correlated pattern expansion itself, not pipeline setup.

## 5. Tests

- `QueryExecutorTest.PatternComprehensionBatchMultipleRows`: three left rows,
  one with no match; verifies per-row lists and the empty list.
- Existing pattern-comprehension / list-comprehension tests.
- Full `query_executor_tests` and `ctest` (excl. TCK); TCK eugraph features.
