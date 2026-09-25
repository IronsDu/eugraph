#!/usr/bin/env python3
"""LDBC SNB Interactive sf0.1: eugraph vs neo4j, interleaved A/B on one machine.

Why interleaved: this box's CPU frequency drifts (docs record the same binary varying by
~2x between sessions), so measuring one engine to completion and then the other turns a
frequency change into a fake speedup. Every round therefore runs both engines back to
back, and the reported figure is the best of N rounds.

Data preparation on the neo4j side (already applied to the local instance, verified by
this script before measuring): derived ``:Message`` label, and ``creationDate`` /
``joinDate`` / ``birthday`` converted from STRING to INTEGER. Without those, complex-5/6/9
match nothing on neo4j and their timings are meaningless.

Id types differ by engine: eugraph stores ``id`` as INTEGER, neo4j as STRING. Id-valued
parameters are cast per engine; nothing else is rewritten.
"""

from __future__ import annotations

import argparse
import importlib.util
import re
import statistics
import sys
import time
from pathlib import Path

try:
    import logging

    from neo4j import GraphDatabase

    # Neo4j 5 通过 driver 的 notification logger 发 "label does not exist" 之类的警告，
    # 每条查询能刷几十行、把结果表冲没。只保留 ERROR。
    logging.getLogger("neo4j.notifications").setLevel(logging.ERROR)
except ImportError:  # pragma: no cover
    sys.exit("needs the 'neo4j' Python driver")

ID_PARAMS = {"personId", "messageId", "forumId", "commentId", "tagId"}


def load_param_parser(script: Path):
    spec = importlib.util.spec_from_file_location("bench_params", script)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.parse_params


def query_body(text: str) -> str:
    """Strip the leading /* ... */ parameter block; both drivers take the bare query."""
    return re.sub(r"/\*.*?\*/", "", text, flags=re.S).strip()


def run_once(driver, database: str, text: str, params: dict) -> tuple[int, float]:
    with driver.session(database=database) as s:
        t0 = time.perf_counter()
        rows = list(s.run(text, **params))
        return len(rows), (time.perf_counter() - t0) * 1000


def check_neo4j_ready(driver) -> None:
    with driver.session(database="neo4j") as s:
        checks = {
            "Message 标签": "MATCH (n:Message) RETURN count(n) AS c",
            "creationDate 为整数": "MATCH (m:Post) RETURN toInteger(m.creationDate) = m.creationDate AS c LIMIT 1",
        }
        for name, q in checks.items():
            val = list(s.run(q))[0]["c"]
            if not val:
                sys.exit(f"neo4j 未准备好：{name} 检查失败")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--queries-dir", required=True)
    ap.add_argument("--eg-uri", default="bolt://127.0.0.1:7688")
    ap.add_argument("--neo-uri", default="bolt://127.0.0.1:7687")
    ap.add_argument("--rounds", type=int, default=15, help="计时轮数（每轮两引擎各一次）")
    ap.add_argument("--warmup", type=int, default=3)
    ap.add_argument("--timeout", type=float, default=120.0)
    ap.add_argument("--skip", default=None)
    ap.add_argument("--only", default=None)
    ap.add_argument("--neo4j-fix-types", action="store_true",
                    help="对 neo4j 侧做类型归一化：旧转换把 LIKES.creationDate 存成 STRING，"
                         "而 eugraph 是 INTEGER，complex-7 的日期相减因此在 neo4j 上报 TypeError。"
                         "开启后只在 neo4j 侧把该字段包一层 toInteger()（eugraph 侧保持原版文本）")
    ap.add_argument("--json", default=None, help="把结果写成 JSON")
    args = ap.parse_args()

    parse_params = load_param_parser(Path(__file__).with_name("bench_ldbc_interactive.py"))
    qdir = Path(args.queries_dir)
    # 只测读查询：interactive-update-* 是写语句，不属本 benchmark
    files = sorted(list(qdir.glob("interactive-complex-*.cypher")) + list(qdir.glob("interactive-short-*.cypher")))
    def matches(f, spec: str) -> bool:
        # spec 形如 "complex-1" / "short-3"；用分隔符边界匹配，避免 "complex-1" 命中 complex-10/11/12
        for part in (p.strip() for p in spec.split(",") if p.strip()):
            if f.stem == f"interactive-{part}" or f.stem.startswith(f"interactive-{part}-"):
                return True
        return False

    if args.only:
        files = [f for f in files if matches(f, args.only)]
    if args.skip:
        files = [f for f in files if not matches(f, args.skip)]

    eg = GraphDatabase.driver(args.eg_uri, auth=None, connection_timeout=args.timeout + 30)
    neo = GraphDatabase.driver(args.neo_uri, auth=None, connection_timeout=args.timeout + 30)
    check_neo4j_ready(neo)

    # 参数：文件默认值 + 文档口径覆盖（personId=933；short 系列用 sf0.1 里真实存在的 messageId）
    OVERRIDES = {"personId": 933, "messageId": 3}

    print(f"{'query':22} {'rows(eg/neo)':>13} {'eg min':>8} {'eg p50':>8} {'neo min':>8} {'neo p50':>8} {'ratio':>7}")
    print("-" * 84)
    out = []
    for f in files:
        text = f.read_text()
        params = parse_params(text)
        for k, v in OVERRIDES.items():
            if k in params:
                params[k] = v
        body = query_body(text)
        neo_body = body
        if args.neo4j_fix_types:
            # 旧转换在 neo4j 侧留下两类 STRING 字段（eugraph 侧是 INTEGER）：
            #   LIKES.creationDate  -> complex-7 的日期相减直接抛 TypeError
            #   WORK_AT.workFrom    -> complex-11 的 `< $workFromYear` 静默不匹配（0 行、不报错）
            # LIKES.creationDate 是字符串，complex-7 把它赋给 likeTime 后再相减
            neo_body = neo_body.replace("like.creationDate AS likeTime", "toInteger(like.creationDate) AS likeTime")
            neo_body = neo_body.replace("workAt.workFrom", "toInteger(workAt.workFrom)")
        eg_params = dict(params)
        neo_params = {k: (str(v) if k in ID_PARAMS else v) for k, v in params.items()}

        eg_times: list[float] = []
        neo_times: list[float] = []
        eg_rows = neo_rows = None
        err = ""
        try:
            # 每个查询换新连接：服务端会断开跑了几十次的长连接（known-defects-todo §7），
            # 复用连接池会把"连接被断开"记成查询失败/变慢。
            eg.close()
            neo.close()
            eg = GraphDatabase.driver(args.eg_uri, auth=None, connection_timeout=args.timeout + 30)
            neo = GraphDatabase.driver(args.neo_uri, auth=None, connection_timeout=args.timeout + 30)
            for _ in range(args.warmup):
                run_once(eg, "default", body, eg_params)
                run_once(neo, "neo4j", neo_body, neo_params)
            for _ in range(args.rounds):
                eg_rows, eg_ms = run_once(eg, "default", body, eg_params)
                neo_rows, neo_ms = run_once(neo, "neo4j", neo_body, neo_params)
                eg_times.append(eg_ms)
                neo_times.append(neo_ms)
        except Exception as exc:  # noqa: BLE001 - report, never hide
            err = f"{type(exc).__name__}: {str(exc).splitlines()[0][:50]}"

        if eg_times and neo_times:
            e_min, n_min = min(eg_times), min(neo_times)
            ratio = n_min / e_min if e_min else float("nan")
            mark = "" if eg_rows == neo_rows else "  <<rows differ>>"
            print(f"{f.stem:22} {f'{eg_rows}/{neo_rows}':>13} {e_min:8.2f} "
                  f"{statistics.median(eg_times):8.2f} {n_min:8.2f} {statistics.median(neo_times):8.2f} "
                  f"{ratio:6.2f}x{mark}")
            out.append({"query": f.stem, "eg_rows": eg_rows, "neo_rows": neo_rows,
                        "eg_min": e_min, "eg_p50": statistics.median(eg_times),
                        "neo_min": n_min, "neo_p50": statistics.median(neo_times), "ratio": ratio})
        else:
            print(f"{f.stem:22} {'-':>13} {'-':>8} {'-':>8} {'-':>8} {'-':>8} {'-':>7}  {err}")

    if args.json:
        import json

        Path(args.json).write_text(json.dumps(out, indent=2))
    eg.close()
    neo.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
