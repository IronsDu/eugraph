# IndexProbe operator and rule split

## Purpose

`Filter(Expand)` access paths need an index probe on the Expand destination.
The probe and the anchor subtree are separate concerns:

- the probe answers `IndexScan(target) -> Expand(target -> source)`;
- the anchor is the complete existing left pipeline (VLE, location filters,
  DISTINCT, ...).

A single rule must not re-select that anchor.

## Operators

```text
BoundIndexProbeOp        leaf probe descriptor
BoundIndexProbeJoinOp    anchor child group + inline probe descriptor
```

`BoundIndexProbeOp` owns only the index/range/edge-step description.
`BoundIndexProbeJoinOp` owns exactly one child group: the anchor.

## Rules

```text
rules/logical/generate_index_probe      Filter(Expand) -> IndexProbe
rules/logical/join_index_probe          IndexProbe + anchor -> IndexProbeJoin
rules/physical/impl_rules               ImplIndexProbeJoin -> physical join
```

Each rule has one job. The logical rules only produce equivalent logical
alternatives. The physical rule only produces a physical candidate; the
cascade framework computes cost and selects the winner.

## Physical lowering

`PhysicalPlanner::planChosen` lowers `IndexProbeJoin` by:

1. planning the anchor child group;
2. lowering the probe to `IndexScanPhysicalOp + ExpandPhysicalOp`;
3. planning a join on `source_variable`.

The physical planner never chooses the anchor or the access path.

## Invariants

- probe never contains an anchor;
- ProbeJoin anchor comes from the existing Group, never a re-derived scan;
- rules do not compute total cost or select winners;
- physical planner only lowers a chosen ProbeJoin descriptor.
