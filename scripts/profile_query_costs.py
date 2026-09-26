#!/usr/bin/env python3
"""C2 investigation: why concurrency makes queries late (LDBC schedule audit failures).

Run against a loaded sf0.1 graph. Reproduces the measurements recorded in
docs/benchmark/ldbc-snb-sf0.1-comparison.md "C2" and pins the two costs apart:

* **row scan + property decode** -- scanning a label and reading a property to filter
  (`Message.creationDate`): ~20 us/row measured (279k rows in 5.7 s).
* **relationship traversal** -- following `HAS_CREATOR` for every message in the graph:
  ~1.9 us/edge (287k edges in 0.53 s), i.e. an order of magnitude cheaper.

That gap is the whole story of complex-9: it visits ~120k messages, and the label scan
path materialises `creationDate` for every row before filtering, while we have no
`Message(creationDate)` index to prune the range (only the `id` unique indexes exist).

Usage:
    scripts/profile_query_costs.py --port 7688 --person-id 32985348834013 \\
        [--concurrency 1,2,4] [--repeat 6]
"""

from __future__ import annotations

import argparse
import statistics
import sys
import time

try:
    from neo4j import GraphDatabase
except ImportError:  # pragma: no cover
    sys.exit("needs the 'neo4j' Python driver")

MAX_DATE = 1346112000000

VARIANTS = {
    "scan label + count (no property)": "MATCH (m:Message) RETURN count(m) AS n",
    "project one property (whole table)": "MATCH (m:Message) RETURN m.creationDate AS cd",
    "scan label + property filter": "MATCH (m:Message) WHERE m.creationDate < $maxd RETURN count(m) AS n",
    "traverse HAS_CREATOR (all)": "MATCH (m:Message)-[:HAS_CREATOR]->(f:Person) RETURN count(m) AS n",
    "2-hop friends of person": "MATCH (r:Person {id:$pid})-[:KNOWS*1..2]-(f:Person) WHERE NOT f=r "
                               "RETURN count(DISTINCT f) AS n",
    "friends' messages (complex-9 core)": "MATCH (r:Person {id:$pid})-[:KNOWS*1..2]-(f:Person) WHERE NOT f=r "
                                          "WITH collect(DISTINCT f) AS fs UNWIND fs AS f "
                                          "MATCH (f)<-[:HAS_CREATOR]-(m:Message) WHERE m.creationDate < $maxd "
                                          "RETURN count(m) AS n",
}


def measure(driver, database: str, query: str, params: dict, repeat: int) -> list[float]:
    """Engine-side timing: consume() drains the stream WITHOUT building Record objects.

    Using list(...) instead measures the Python driver's result construction, which for
    ~3e5 rows costs ~10.2 s against the engine's 43 ms -- a 238x error that once produced
    a bogus "vertex materialisation is the bottleneck" conclusion. Keep this as consume().
    """
    times = []
    with driver.session(database=database) as session:
        for _ in range(repeat):
            start = time.perf_counter()
            session.run(query, **params).consume()
            times.append((time.perf_counter() - start) * 1000)
    return times


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=7688)
    ap.add_argument("--database", default="default")
    ap.add_argument("--person-id", type=int, default=32985348834013)
    ap.add_argument("--repeat", type=int, default=6)
    ap.add_argument("--compare-neo4j", action="store_true",
                    help="also run each variant against neo4j on port 7687")
    args = ap.parse_args()

    params = {"pid": args.person_id, "maxd": MAX_DATE}
    eugraph = GraphDatabase.driver(f"bolt://{args.host}:{args.port}",
                                   auth=("neo4j", "eugraph"), connection_timeout=120)
    engines = [("eugraph", eugraph, args.database, params)]
    if args.compare_neo4j:
        neo4j = GraphDatabase.driver("bolt://127.0.0.1:7687", auth=None, connection_timeout=120)
        engines.append(("neo4j", neo4j, "neo4j",
                        {"pid": str(args.person_id), "maxd": MAX_DATE}))

    for name, query in VARIANTS.items():
        print(f"\n### {name}")
        for engine, driver, database, p in engines:
            times = measure(driver, database, query, p, args.repeat)
            print(f"  {engine:8} min={min(times):9.1f}  p50={statistics.median(times):9.1f}  "
                  f"max={max(times):9.1f}  (ms, n={len(times)})")
    for _, driver, _, _ in engines:
        driver.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
