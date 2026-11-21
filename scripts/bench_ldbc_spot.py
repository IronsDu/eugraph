#!/usr/bin/env python3
"""LDBC SF1 spot benchmark for c6/c9 (read-only, one engine).

This is deliberately small and reproducible: it resolves real person/tag values
from the target database, runs the official query files with 2 warmups + 5
timed iterations, and prints min/p50. It is not a replacement for the full
official driver; it is a quick regression check.

Usage:
    python3 scripts/bench_ldbc_spot.py \
        --uri bolt://127.0.0.1:7688 --database sf1 \
        --queries /path/to/ldbc_snb_interactive_v1_impls/cypher/queries
"""

from __future__ import annotations

import argparse
import re
import statistics
import time
from pathlib import Path

from neo4j import GraphDatabase


def load_query(path: Path) -> str:
    text = path.read_text()
    return re.sub(r"/\*.*?\*/", "", text, flags=re.S).strip()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--uri", default="bolt://127.0.0.1:7688")
    ap.add_argument("--database", default="sf1")
    ap.add_argument("--queries", required=True, type=Path)
    ap.add_argument("--warmup", type=int, default=2)
    ap.add_argument("--iters", type=int, default=5)
    args = ap.parse_args()

    driver = GraphDatabase.driver(args.uri, auth=None, connection_timeout=30)
    try:
        with driver.session(database=args.database) as s:
            person = list(s.run("MATCH (n:Person) RETURN n.id AS id LIMIT 1"))[0]["id"]
            tag = list(s.run("MATCH (n:Tag) RETURN n.name AS name LIMIT 1"))[0]["name"]

        cases = [
            ("c6", args.queries / "interactive-complex-6.cypher",
             {"personId": person, "tagName": tag}),
            ("c9", args.queries / "interactive-complex-9.cypher",
             {"personId": person, "maxDate": 1289908800000}),
        ]
        for name, path, params in cases:
            query = load_query(path)
            with driver.session(database=args.database) as s:
                for _ in range(args.warmup):
                    list(s.run(query, **params))
                times = []
                rows = 0
                for _ in range(args.iters):
                    t0 = time.perf_counter()
                    rows = len(list(s.run(query, **params)))
                    times.append((time.perf_counter() - t0) * 1000.0)
            p50 = statistics.median(times)
            print(f"{name}: rows={rows} min={min(times):.2f}ms p50={p50:.2f}ms")
        return 0
    finally:
        driver.close()


if __name__ == "__main__":
    raise SystemExit(main())
