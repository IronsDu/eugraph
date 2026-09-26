#!/usr/bin/env python3
"""Per-query latency as a function of concurrency, for one engine (or two, for comparison).

Why this exists: the LDBC official driver's schedule audit failed with
TOO_MANY_LATE_OPERATIONS, i.e. queries finishing seconds late while our own interleaved A/B
(one connection per query) reported the same queries in milliseconds. Those two numbers are
not comparable -- the official driver keeps a pool of long-lived connections, so the question
is not "how fast is this query" but "how fast is it when N of them run at once".

Each worker owns a persistent connection and runs the same (query, params) repeatedly,
recording the latency of every execution. Reported per worker: min / p50 / p95 / max; plus the
aggregate throughput. Run the same command against eugraph and neo4j to compare.

Usage:
    scripts/profile_concurrency.py --port 7688 --database default --threads 4 \
        --query-file <interactive-complex-9.cypher> --param personId=933 --param maxDate=... \
        --seconds 20

    # sweep concurrency 1/2/4/8 against one engine, printing a table
    scripts/profile_concurrency.py --port 7688 --sweep 1,2,4,8 --query-file ...
"""

from __future__ import annotations

import argparse
import re
import statistics
import sys
import threading
import time
from pathlib import Path

try:
    from neo4j import GraphDatabase
except ImportError:  # pragma: no cover
    sys.exit("needs the 'neo4j' Python driver")


def load_query(path: str | None, query_text: str | None) -> str:
    if query_text:
        return query_text
    if not path:
        return "RETURN 1 AS n"
    return re.sub(r"/\*.*?\*/", "", Path(path).read_text(), flags=re.S).strip()


def parse_params(items: list[str]) -> dict:
    out: dict = {}
    for item in items:
        if "=" not in item:
            continue
        key, value = item.split("=", 1)
        key, value = key.strip(), value.strip()
        out[key] = int(value) if value.lstrip("-").isdigit() else value
    return out


def run_worker(uri: str, database: str, query: str, params: dict, seconds: float,
               barrier: threading.Barrier, out: dict, label: str, errors: list) -> None:
    times: list[float] = []
    driver = None
    try:
        driver = GraphDatabase.driver(uri, auth=("neo4j", "eugraph"), connection_timeout=30)
        with driver.session(database=database) as session:
            barrier.wait()
            deadline = time.monotonic() + seconds
            while time.monotonic() < deadline:
                start = time.perf_counter()
                try:
                    list(session.run(query, **params))
                except Exception as exc:  # noqa: BLE001 - the failure is data too
                    errors.append(f"{label}: {type(exc).__name__}: {str(exc).splitlines()[0][:80]}")
                    return
                times.append((time.perf_counter() - start) * 1000)
        out[label] = times
    finally:
        if driver is not None:
            driver.close()


def sweep_once(uri: str, database: str, query: str, params: dict, threads: int,
               seconds: float) -> dict:
    out: dict = {}
    errors: list = []
    barrier = threading.Barrier(threads)
    workers = [
        threading.Thread(target=run_worker,
                         args=(uri, database, query, params, seconds, barrier, out, f"c{i}", errors))
        for i in range(threads)
    ]
    started = time.perf_counter()
    for t in workers:
        t.start()
    for t in workers:
        t.join()
    wall = time.perf_counter() - started

    all_times = [t for times in out.values() for t in times]
    if not all_times:
        return {"threads": threads, "ops": 0, "error": errors[0] if errors else "no executions"}
    return {
        "threads": threads,
        "ops": len(all_times),
        "throughput": len(all_times) / wall,
        "min": min(all_times),
        "p50": statistics.median(all_times),
        "p95": sorted(all_times)[int(len(all_times) * 0.95) - 1] if len(all_times) > 1 else all_times[0],
        "max": max(all_times),
        "wall": wall,
        "error": errors[0] if errors else "",
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=7688)
    ap.add_argument("--database", default="default")
    ap.add_argument("--query-file", default=None)
    ap.add_argument("--query", default=None, help="inline query (overrides --query-file)")
    ap.add_argument("--param", action="append", default=[], help="key=value, repeatable")
    ap.add_argument("--threads", type=int, default=1)
    ap.add_argument("--sweep", default=None, help="comma-separated thread counts, e.g. 1,2,4,8")
    ap.add_argument("--seconds", type=float, default=15.0, help="measurement window per step")
    ap.add_argument("--warmup-seconds", type=float, default=3.0)
    args = ap.parse_args()

    uri = f"bolt://{args.host}:{args.port}"
    query = load_query(args.query_file, args.query)
    params = parse_params(args.param)
    steps = [int(x) for x in args.sweep.split(",")] if args.sweep else [args.threads]

    print(f"target : {uri} database={args.database}")
    print(f"query  : {query.splitlines()[0][:90]}")
    print(f"params : {params}")
    print(f"window : warmup {args.warmup_seconds}s + measure {args.seconds}s per step\n")

    print(f"{'threads':>7} {'ops':>6} {'ops/s':>8} {'min':>9} {'p50':>9} {'p95':>9} {'max':>10}")
    for n in steps:
        sweep_once(uri, args.database, query, params, n, args.warmup_seconds)  # warmup
        r = sweep_once(uri, args.database, query, params, n, args.seconds)
        if r["ops"] == 0:
            print(f"{n:>7} {'-':>6} {'-':>8} {'-':>9} {'-':>9} {'-':>9} {'-':>10}  {r['error']}")
        else:
            print(f"{n:>7} {r['ops']:>6} {r['throughput']:>8.2f} {r['min']:>9.1f} "
                  f"{r['p50']:>9.1f} {r['p95']:>9.1f} {r['max']:>10.1f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
