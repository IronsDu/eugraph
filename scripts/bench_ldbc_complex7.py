#!/usr/bin/env python3
"""LDBC SNB Interactive complex-7 comparative benchmark (EuGraph vs Neo4j).

Motivation
----------
The earlier SF0.1 comparison measured complex-7 with `personId=1242` and reported
`EuGraph 58ms / Neo4j 6ms`. That comparison was not like-for-like:

* Neo4j's sf0.1 import has no `:Message` parent label (only `:Comment` / `:Post`),
  so `MATCH (message:Message)` matched nothing. Its plan was
  `NodeByLabelScan(message:Message)` -> 0 rows, i.e. it never did the work.
* Neo4j's import stored `creationDate` as STRING, so the verbatim LDBC text
  (`latestLike.likeTime - latestLike.msg.creationDate`) raises
  `CypherTypeError: Cannot subtract String from String` there.
  EuGraph's loader infers the column as INT64, so the verbatim text runs.

This script therefore measures two equivalence levels:

``orig``
    Verbatim LDBC complex-7 text. EuGraph-native; on Neo4j it degenerates to
    0 rows (recorded for transparency, NOT as a baseline).
``eqv``
    Label/type-normalised text that returns identical rows on both engines:
    ``WHERE (message:Comment OR message:Post)`` and ``toInteger(...)`` around
    the date operands (needed because Neo4j stores them as STRING).

Usage
-----
    scripts/bench_ldbc_complex7.py --engine eugraph --variant eqv --person-id 1242
    scripts/bench_ldbc_complex7.py --both --person-id 2199023256816

Requires a running Bolt endpoint for each engine and the `neo4j` Python driver.
"""

from __future__ import annotations

import argparse
import statistics
import sys
import time
from pathlib import Path

try:
    from neo4j import GraphDatabase
except ImportError:  # pragma: no cover
    sys.exit("the 'neo4j' Python driver is required: pip install neo4j")

LDBC_QUERY = Path(
    "/home/dodo/code/fuck/ldbc_snb_interactive_v1_impls/cypher/queries/interactive-complex-7.cypher"
)

# Label/type-normalised body. %LATENCY% differs per engine because Neo4j stores
# creationDate as STRING while EuGraph infers INT64.
EQV_BODY = """MATCH (person:Person {id: $personId})<-[:HAS_CREATOR]-(message)<-[like:LIKES]-(liker:Person)
    WHERE (message:Comment OR message:Post)
    WITH liker, message, like.creationDate AS likeTime, person
    ORDER BY likeTime DESC, toInteger(message.id) ASC
    WITH liker, head(collect({msg: message, likeTime: likeTime})) AS latestLike, person
RETURN
    liker.id AS personId,
    liker.firstName AS personFirstName,
    liker.lastName AS personLastName,
    latestLike.likeTime AS likeCreationDate,
    latestLike.msg.id AS commentOrPostId,
    coalesce(latestLike.msg.content, latestLike.msg.imageFile) AS commentOrPostContent,
    %LATENCY% AS minutesLatency,
    not((liker)-[:KNOWS]-(person)) AS isNew
ORDER BY
    likeCreationDate DESC,
    toInteger(personId) ASC
LIMIT 20"""

EQV_LATENCY = {
    # EuGraph: creationDate is INT64 -> verbatim arithmetic, like the LDBC text.
    "eugraph": "toInteger(floor(toFloat(latestLike.likeTime - latestLike.msg.creationDate)/1000.0)/60.0)",
    # Neo4j: creationDate imported as STRING -> must coerce both operands.
    "neo4j": (
        "toInteger(floor(toFloat(toInteger(latestLike.likeTime) - "
        "toInteger(latestLike.msg.creationDate))/1000.0)/60.0)"
    ),
}

ENGINES = {
    "eugraph": {"uri": "bolt://127.0.0.1:7688", "database": "neo4j", "id_type": int},
    "neo4j": {"uri": "bolt://127.0.0.1:7687", "database": "neo4j", "id_type": str},
}


def build_query(engine: str, variant: str) -> str:
    if variant == "orig":
        return LDBC_QUERY.read_text()
    return EQV_BODY.replace("%LATENCY%", EQV_LATENCY[engine])


def bench(engine: str, variant: str, person_id: str, warmup: int, iters: int) -> dict:
    cfg = ENGINES[engine]
    query = build_query(engine, variant)
    param = cfg["id_type"](person_id)

    session_kwargs = {}
    if engine == "neo4j":
        session_kwargs["notifications_min_severity"] = "OFF"

    driver = GraphDatabase.driver(cfg["uri"], auth=None)
    try:
        with driver.session(database=cfg["database"], **session_kwargs) as session:
            rows = list(session.run(query, personId=param))
            times = []
            for i in range(warmup + iters):
                started = time.perf_counter()
                list(session.run(query, personId=param))
                elapsed = (time.perf_counter() - started) * 1000.0
                if i >= warmup:
                    times.append(elapsed)
    finally:
        driver.close()

    return {
        "engine": engine,
        "variant": variant,
        "person_id": person_id,
        "rows": len(rows),
        "median_ms": round(statistics.median(times), 2),
        "min_ms": round(min(times), 2),
        # min is the most stable statistic on a shared/noisy machine: it is the
        # run least perturbed by other load. p25 is reported alongside.
        "p25_ms": round(sorted(times)[len(times) // 4], 2) if times else None,
        "times_ms": [round(t, 2) for t in times],
        "first_row": {k: str(v)[:40] for k, v in dict(rows[0]).items()} if rows else None,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--engine", choices=sorted(ENGINES), help="engine to benchmark")
    parser.add_argument("--both", action="store_true", help="benchmark both engines")
    parser.add_argument("--variant", choices=["orig", "eqv"], default="eqv")
    parser.add_argument("--person-id", default="1242",
                        help="LDBC personId parameter; comma-separated for several (e.g. 933,1242)")
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--iters", type=int, default=8)
    args = parser.parse_args()

    engines = sorted(ENGINES) if args.both else ([args.engine] if args.engine else [])
    if not engines:
        parser.error("pass --engine or --both")

    person_ids = [p.strip() for p in args.person_id.split(",") if p.strip()]
    # (person_id) -> {engine: result}
    table: dict[str, dict[str, dict]] = {}
    for person_id in person_ids:
        for engine in engines:
            try:
                res = bench(engine, args.variant, person_id, args.warmup, args.iters)
            except Exception as exc:  # noqa: BLE001 - surface driver errors as data
                print(f"{engine:8s} {args.variant:4s} pid={person_id} ERROR {type(exc).__name__}: {str(exc)[:120]}")
                continue
            table.setdefault(person_id, {})[engine] = res
            print(
                f"{res['engine']:8s} {res['variant']:4s} pid={res['person_id']:>18s} "
                f"rows={res['rows']:3d} min={res['min_ms']:8.2f}ms p25={res['p25_ms']:8.2f}ms "
                f"median={res['median_ms']:8.2f}ms"
            )
            if res["first_row"]:
                print(f"         first row: {res['first_row']}")

    # Like-for-like assertion: the two engines must observe the same result.
    mismatch = [
        (pid, table[pid]["eugraph"]["rows"], table[pid]["neo4j"]["rows"])
        for pid in table
        if "eugraph" in table[pid] and "neo4j" in table[pid]
        and table[pid]["eugraph"]["rows"] != table[pid]["neo4j"]["rows"]
    ]
    if mismatch:
        print("\nWARNING: row counts differ - comparison is NOT like-for-like:")
        for pid, eg_rows, neo_rows in mismatch:
            print(f"  pid={pid}: eugraph={eg_rows} neo4j={neo_rows}")

    ratios = [
        (pid, table[pid]["eugraph"]["min_ms"] / table[pid]["neo4j"]["min_ms"])
        for pid in table
        if "eugraph" in table[pid] and "neo4j" in table[pid] and table[pid]["neo4j"]["min_ms"] > 0
    ]
    if ratios:
        print("\nratio (eugraph/neo4j, min):")
        for pid, r in ratios:
            print(f"  pid={pid:>18s}  {r:.2f}x")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
