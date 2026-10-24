# c5/c9 Operator Implementation Plan

## c9
Current plan: Limit -> Project -> Sort -> Filter -> PE (content materialized before Sort).
Implement `LateMaterializePhysicalOp`; move output Project above Limit; defer only coalesce properties not used by Filter/Sort; new op must own full output schema/slot layout; add column/value parity tests. Target 420ms -> <=250ms.

## c5
Neo4j also expands 89k posts/creators; eugraph gap is per-source Expand. Implement `BatchExpandPhysicalOp` for single-label fixed Expand: buffer child, if rows <=2048 use per-source path; else scanEdgesByType once + hash join. Preserve LeftJoin/OPTIONAL zero-count. Target 514ms -> <=300ms.

## State
Branch `perf/ldbc-c5-c9-c14`, HEAD `a498b82a`; c14 80.5s -> 0.81s done. Tests: ctest 1304/1304 last full run.
