#!/usr/bin/env python3
"""Drive one Bolt connection through many executions, to expose/verify connection drops.

Background: the read buffer used to never reclaim its consumed prefix, so a single
connection died once the cumulative byte count reached the buffer size (64 KiB) -- see
"连接生命周期与读缓冲回卷" in docs/service/neo4j-bolt-protocol.md. The failure was a pure
function of bytes per execution, not of query semantics:

    RETURN 1           (~78 B/exec)   -> dropped around execution 840
    RETURN '<1.5 KB>'  (~1560 B/exec) -> dropped around execution 41

That defect is fixed; this script is the tool that shows it on a real connection, which the
unit tests only cover through the driver.

Why not just use the benchmark scripts: they open a connection per query, which is exactly
what hid the bug. The LDBC official driver opens long-lived connections and cannot be told
to reconnect, so a drop there terminates a whole benchmark run.

Usage:
    # reproducibility check against any running server (start it separately so its log
    # stays available; a drop also prints "getReadBuffer() returned empty buffer" there)
    scripts/repro_bolt_connection.py --port 7688 --database default --queries 300

    # the shape that used to fail fastest, with the official query file
    scripts/repro_bolt_connection.py --port 7688 --queries 300 --query-file <interactive-short-2.cypher>

Exit code: 0 if every execution completed, 1 on the first failure (with its index).
"""

from __future__ import annotations

import argparse
import re
import sys
import threading
import time
from pathlib import Path

try:
    from neo4j import GraphDatabase
except ImportError:  # pragma: no cover
    sys.exit("needs the 'neo4j' Python driver")


DEFAULT_QUERY = "MATCH (p:Person {id: $personId}) RETURN p.firstName AS name"


def load_query(path: str | None) -> tuple[str, dict]:
    """Return (query_text, params). Strips an LDBC /* :param */ header like the bench scripts."""
    if not path:
        return DEFAULT_QUERY, {"personId": 933}
    text = Path(path).read_text()
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S).strip()
    return text, {"personId": 933}


def run_worker(uri: str, database: str, query: str, params: dict, count: int, label: str,
               print_every: int, out: dict, barrier: threading.Barrier) -> None:
    """One connection (= one driver), executing `count` queries sequentially."""
    driver = GraphDatabase.driver(uri, auth=("neo4j", "eugraph"), connection_timeout=30)
    done = 0
    try:
        with driver.session(database=database) as session:
            barrier.wait()  # align the workers so they contend on the same window
            for i in range(count):
                start = time.perf_counter()
                try:
                    rows = list(session.run(query, **params))
                except Exception as exc:  # noqa: BLE001 - the failure IS the result
                    out[label] = ("failed", i, type(exc).__name__, str(exc).splitlines()[0][:120],
                                  (time.perf_counter() - start) * 1000)
                    return
                done = i + 1
                if print_every and done % print_every == 0:
                    print(f"  [{label}] {done}/{count} ok, last={rows and len(rows)} rows, "
                          f"{(time.perf_counter() - start) * 1000:.1f} ms", flush=True)
        out[label] = ("ok", done, "", "", 0.0)
    finally:
        driver.close()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=7688)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--database", default="default")
    ap.add_argument("--queries", type=int, default=200, help="executions per connection")
    ap.add_argument("--threads", type=int, default=1, help="number of concurrent connections")
    ap.add_argument("--query-file", default=None, help="optional .cypher file (LDBC style)")
    ap.add_argument("--print-every", type=int, default=25)
    args = ap.parse_args()

    uri = f"bolt://{args.host}:{args.port}"
    query, params = load_query(args.query_file)
    print(f"target   : {uri} database={args.database}")
    print(f"query    : {query.splitlines()[0][:80]}")
    print(f"workload : {args.threads} connection(s) x {args.queries} executions "
          f"(= {args.threads * args.queries} total)")

    out: dict = {}
    barrier = threading.Barrier(args.threads)
    threads = [
        threading.Thread(target=run_worker,
                         args=(uri, args.database, query, params, args.queries, f"c{i}",
                               args.print_every, out, barrier))
        for i in range(args.threads)
    ]
    started = time.perf_counter()
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    elapsed = time.perf_counter() - started

    print(f"\n=== result ({elapsed:.1f}s wall) ===")
    failed = 0
    for label in sorted(out):
        status, n, kind, msg, ms = out[label]
        if status == "ok":
            print(f"  {label}: OK  completed all {n} executions")
        else:
            failed += 1
            print(f"  {label}: FAILED after {n} execution(s) -> {kind}: {msg}  "
                  f"(last attempt took {ms:.0f} ms)")
    for label in range(args.threads):
        key = f"c{label}"
        if key not in out:
            failed += 1
            print(f"  {key}: NO RESULT (thread died)")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
