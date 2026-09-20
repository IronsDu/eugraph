#!/usr/bin/env python3
"""LDBC SNB Interactive benchmark over the verbatim query set.

Runs every ``interactive-complex-N.cypher`` / ``interactive-short-N.cypher`` against a
Bolt endpoint, taking the parameter defaults from each file's ``:param`` comment and
overriding ``personId`` with one that exists in the loaded dataset (the LDBC defaults
are not the ids the SF0.1 conversion produced).

Queries that cannot run are reported rather than hidden: server-side errors (missing
features such as shortestPath) and timeouts are both surfaced in the output.

Usage
-----
    scripts/bench_ldbc_interactive.py --queries-dir <dir> --uri bolt://127.0.0.1:7688 \
        --person-id 933 --warmup 1 --iters 3
"""

from __future__ import annotations

import argparse
import re
import statistics
import sys
import time
from pathlib import Path

try:
    from neo4j import GraphDatabase
except ImportError:  # pragma: no cover
    sys.exit("needs the 'neo4j' Python driver")


PARAM_BLOCK = re.compile(r":param\s*\[\{([^}]*)\}\]\s*=>\s*\{(.*?)\}", re.S)


def parse_params(text: str) -> dict:
    """Pull the default parameter values out of the query file's :param comment."""
    m = PARAM_BLOCK.search(text)
    if not m:
        return {}
    names = [n.strip() for n in m.group(1).split(",") if n.strip()]
    body = m.group(2)
    out: dict = {}
    for name in names:
        # The block reads `<value> AS <name>`, so anchor on the alias and take the
        # token that precedes it on the same line.
        lit = re.search(rf"\bAS\s+{re.escape(name)}\b", body)
        if not lit:
            continue
        segment = body[: lit.start()].rstrip().rstrip(",")
        token = segment.splitlines()[-1].strip() if segment else ""
        if not token:
            continue
        if token.startswith('"') and token.endswith('"'):
            out[name] = token[1:-1]
        elif token.lstrip("-").isdigit():
            out[name] = int(token)
        else:
            out[name] = token
    return out


def queries_in(root: Path) -> list[Path]:
    files = sorted(root.glob("interactive-complex-*.cypher")) + sorted(root.glob("interactive-short-*.cypher"))
    return [f for f in files if "parameters" not in f.name]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--queries-dir", required=True)
    ap.add_argument("--uri", default="bolt://127.0.0.1:7688")
    ap.add_argument("--user", default=None)
    ap.add_argument("--password", default=None)
    ap.add_argument("--person-id", type=int, default=933)
    ap.add_argument("--warmup", type=int, default=1)
    ap.add_argument("--iters", type=int, default=3)
    ap.add_argument("--timeout", type=float, default=60.0, help="per-execution timeout in seconds")
    ap.add_argument("--only", default=None, help="comma-separated substrings to restrict to")
    ap.add_argument("--skip", default=None, help="comma-separated substrings to skip")
    args = ap.parse_args()

    auth = (args.user, args.password) if args.user else None
    root = Path(args.queries_dir)
    files = queries_in(root)
    if args.only:
        wanted = [s.strip() for s in args.only.split(",")]
        files = [f for f in files if any(w in f.name for w in wanted)]
    if args.skip:
        unwanted = [s.strip() for s in args.skip.split(",")]
        files = [f for f in files if not any(w in f.name for w in unwanted)]

    driver = GraphDatabase.driver(args.uri, auth=auth, connection_timeout=args.timeout + 30)
    print(f"{'query':34s} {'rows':>5s} {'min(ms)':>9s} {'median(ms)':>11s}  note")
    print("-" * 78)
    for f in files:
        text = f.read_text()
        params = parse_params(text)
        if "personId" in params:
            params["personId"] = args.person_id
        # The endpoint may be int-typed (eugraph) or string-typed (neo4j imports).
        attempts = [params]
        if "personId" in params:
            alt = dict(params, personId=str(params["personId"]))
            attempts.append(alt)
        row_count = None
        times: list[float] = []
        note = ""
        for attempt in attempts:
            times, row_count, note = [], None, ""
            try:
                with driver.session() as s:
                    for _ in range(args.warmup):
                        list(s.run(text, **attempt))
                    for _ in range(args.iters):
                        t0 = time.perf_counter()
                        rows = list(s.run(text, **attempt))
                        times.append((time.perf_counter() - t0) * 1000)
                    row_count = len(rows)
                break
            except Exception as exc:  # noqa: BLE001 - report, never hide
                note = f"{type(exc).__name__}: {str(exc).splitlines()[0][:60]}"
                times = []
        if times:
            print(f"{f.stem:34s} {row_count:5d} {min(times):9.1f} {statistics.median(times):11.1f}")
        else:
            print(f"{f.stem:34s} {'-':>5s} {'-':>9s} {'-':>11s}  {note}")
    driver.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
