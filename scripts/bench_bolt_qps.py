#!/usr/bin/env python3
"""Concurrent Bolt QPS for eugraph (N long-lived connections, fixed-duration loop).

Why a separate script: ``bench_ldbc_ab.py`` opens a new connection per query, which
deliberately hides connection-level behaviour. This one keeps one connection per
client (like the official LDBC driver) and measures aggregate throughput under
concurrency, so it can compare against the QPS numbers recorded for the
session/cursor concurrency work (docs/architecture/system-architecture.md).

Method: warm up each client, align all clients on a barrier, then every client runs
the same query back-to-back until the duration expires. Reported QPS is
completed_batches / wall, where a batch = one query returning all rows.

Usage (complex-9 from the LDBC sf0.1 query set):
    scripts/bench_bolt_qps.py \
        --query-file /home/dodo/code/fuck/ldbc_snb_interactive_v1_impls/cypher/queries/interactive-complex-9.cypher \
        --clients 8 --duration 30 --person-id 933

Keep the server's ``--compute-threads`` >= ``--clients`` when measuring peak QPS;
with fewer compute threads the run is compute-bound (documented trade-off).
"""

from __future__ import annotations

import argparse
import re
import statistics
import threading
import time
from pathlib import Path

try:
    from neo4j import GraphDatabase
except ImportError:  # pragma: no cover
    raise SystemExit("needs the 'neo4j' Python driver")


def query_body(path: Path) -> str:
    """Strip the LDBC ``/* :param ... */`` header; drivers receive the bare query."""
    return re.sub(r"/\*.*?\*/", "", path.read_text(), flags=re.S).strip()


def run_client(uri: str, database: str, body: str, params: dict, duration: float, warmup: int,
               barrier: threading.Barrier, out: dict, index: int) -> None:
    driver = GraphDatabase.driver(uri, auth=None, connection_timeout=60)
    latencies: list[float] = []
    error = ""
    try:
        with driver.session(database=database) as session:
            for _ in range(warmup):
                list(session.run(body, **params))
            barrier.wait()
            deadline = time.perf_counter() + duration
            while time.perf_counter() < deadline:
                started = time.perf_counter()
                list(session.run(body, **params))
                latencies.append((time.perf_counter() - started) * 1000.0)
    except Exception as exc:  # noqa: BLE001 - the failure is part of the measurement
        error = f"{type(exc).__name__}: {str(exc).splitlines()[0][:160]}"
    finally:
        driver.close()
    out[index] = (latencies, error)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--uri", default="bolt://127.0.0.1:7688")
    ap.add_argument("--database", default="default")
    ap.add_argument("--query-file", required=True)
    ap.add_argument("--clients", type=int, default=1)
    ap.add_argument("--duration", type=float, default=20.0, help="measured seconds after warmup")
    ap.add_argument("--warmup", type=int, default=2, help="warmup executions per client")
    ap.add_argument("--person-id", type=int, default=933)
    ap.add_argument("--max-date", type=int, default=1289908800000)
    args = ap.parse_args()
    if args.clients <= 0 or args.duration <= 0:
        ap.error("--clients/--duration must be positive")

    body = query_body(Path(args.query_file))
    params = {"personId": args.person_id, "maxDate": args.max_date}

    out: dict[int, tuple[list[float], str]] = {}
    barrier = threading.Barrier(args.clients + 1)
    threads = [
        threading.Thread(target=run_client,
                         args=(args.uri, args.database, body, params, args.duration, args.warmup,
                               barrier, out, i))
        for i in range(args.clients)
    ]
    for thread in threads:
        thread.start()
    barrier.wait()
    started = time.perf_counter()
    for thread in threads:
        thread.join()
    wall = time.perf_counter() - started

    latencies: list[float] = []
    errors: list[str] = []
    for i in range(args.clients):
        client_latencies, client_error = out.get(i, ([], "no result"))
        latencies.extend(client_latencies)
        if client_error:
            errors.append(f"c{i}: {client_error}")

    completed = len(latencies)
    print(f"clients={args.clients} duration={wall:.2f}s completed={completed} qps={completed / wall:.2f}")
    if latencies:
        latencies.sort()
        p50 = statistics.median(latencies)
        p95 = latencies[min(len(latencies) - 1, int(len(latencies) * 0.95))]
        print(f"lat_ms min={latencies[0]:.1f} p50={p50:.1f} p95={p95:.1f} max={latencies[-1]:.1f}")
    if errors:
        print("ERRORS: " + "; ".join(errors))
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
