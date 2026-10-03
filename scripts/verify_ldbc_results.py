#!/usr/bin/env python3
"""LDBC SNB Interactive 查询**结果**对照：eugraph vs neo4j（逐行比对，不是计时）。

为什么要有这个脚本：
  `bench_ldbc_ab.py` 只比**耗时**——两边都返回 0 行时它会给出"漂亮的"数字。数据装载/索引
  改动后真正要回答的是"两边结果是否一致"。本脚本复用同一套口径（参数覆盖、id 按引擎转型、
  neo4j 侧类型改写、就绪检查），但把每一行结果取回来做**多重集比对**。

默认跳过文档记录为"无法成功运行"的语句（`docs/benchmark/ldbc-snb-sf0.1-comparison.md` §3）：
  complex-1 / complex-13 / complex-14  —— shortestPath / allShortestPaths + reduce 语法不支持
  complex-5                            —— eugraph 不返回（compute 满载、取消不生效）
需要时用 --only 显式指定，或 --skip "" 取消默认跳过。

用法：
  python3 scripts/verify_ldbc_results.py --queries-dir <dir> [--only complex-7] [--skip ""]
退出码：0 = 全部一致；1 = 存在差异/错误。
"""
from __future__ import annotations

import argparse
import importlib.util
import sys
import time
from pathlib import Path

from neo4j import GraphDatabase

QUERIES_ROOT = Path("/home/dodo/code/fuck/ldbc_snb_interactive_v1_impls/cypher/queries")
# 文档 §3 记录为"无法成功运行"，默认跳过
DEFAULT_SKIP = "complex-1,complex-5,complex-13,complex-14"

sys.path.insert(0, str(Path(__file__).parent))
from bench_ldbc_ab import ID_PARAMS, check_neo4j_ready, load_param_parser, query_body  # noqa: E402


def normalize(value):
    """把结果值归一化，消除两个驱动在数值/字符串表示上的差异。"""
    if isinstance(value, bool):
        return ("bool", value)
    if isinstance(value, (int, float)):
        return ("num", float(value))
    if isinstance(value, str):
        # id / 日期在两个引擎里的**类型不同是已知且有意的**（eugraph INTEGER、neo4j STRING，
        # 见 docs/benchmark §4）。不把"数字字符串"归一到数字，会把每一条 id 列都报成差异。
        try:
            return ("num", float(value))
        except ValueError:
            return ("str", value)
    if isinstance(value, (list, tuple)):
        return ("list", tuple(normalize(v) for v in value))
    if value is None:
        return ("null",)
    return ("other", str(value))


def fetch(driver, database, body, params, timeout):
    rows = []
    started = time.perf_counter()
    with driver.session(database=database) as session:
        for record in session.run(body, **params):
            rows.append(tuple(normalize(v) for v in record.values()))
    return rows, (time.perf_counter() - started) * 1000


def matches(stem: str, spec: str) -> bool:
    for part in (p.strip() for p in spec.split(",") if p.strip()):
        if stem == f"interactive-{part}" or stem.startswith(f"interactive-{part}-"):
            return True
    return False


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--queries-dir", default=str(QUERIES_ROOT))
    ap.add_argument("--eg-uri", default="bolt://127.0.0.1:7688")
    ap.add_argument("--eg-db", default="default")
    ap.add_argument("--neo-uri", default="bolt://127.0.0.1:7687")
    ap.add_argument("--neo-db", default="neo4j")
    ap.add_argument("--neo-user", default="neo4j")
    ap.add_argument("--neo-password", default="eugraph")
    ap.add_argument("--timeout", type=float, default=90.0)
    ap.add_argument("--only", default=None)
    ap.add_argument("--skip", default=DEFAULT_SKIP)
    args = ap.parse_args()

    parse_params = load_param_parser(Path(__file__).with_name("bench_ldbc_interactive.py"))
    root = Path(args.queries_dir)
    files = sorted(list(root.glob("interactive-complex-*.cypher")) + list(root.glob("interactive-short-*.cypher")))
    if args.only:
        files = [f for f in files if matches(f.stem, args.only)]
    if args.skip:
        files = [f for f in files if not matches(f.stem, args.skip)]

    eg = GraphDatabase.driver(args.eg_uri, auth=None, connection_timeout=args.timeout + 30)
    neo = GraphDatabase.driver(args.neo_uri, auth=(args.neo_user, args.neo_password),
                               connection_timeout=args.timeout + 30)
    check_neo4j_ready(neo)

    # 文档口径的参数覆盖（sf0.1 里真实存在的 id）
    overrides = {"personId": 933, "messageId": 3}

    print(f"{'query':22} {'eg rows':>8} {'neo rows':>9} {'eg ms':>8} {'neo ms':>8}  result")
    print("-" * 74)
    mismatches = 0
    for f in files:
        text = f.read_text()
        params = parse_params(text)
        for k, v in overrides.items():
            if k in params:
                params[k] = v
        body = query_body(text)
        # 文档 §1.1 #3/#4：neo4j 侧旧转换留下两类 STRING 字段（eugraph 侧是 INTEGER）
        neo_body = body.replace("like.creationDate AS likeTime", "toInteger(like.creationDate) AS likeTime")
        neo_body = neo_body.replace("workAt.workFrom", "toInteger(workAt.workFrom)")
        eg_params = dict(params)
        neo_params = {k: (str(v) if k in ID_PARAMS else v) for k, v in params.items()}

        eg_rows = neo_rows = None
        eg_ms = neo_ms = float("nan")
        note = "MATCH"
        try:
            eg_rows, eg_ms = fetch(eg, args.eg_db, body, eg_params, args.timeout)
        except Exception as exc:  # noqa: BLE001
            note = f"EG-ERR {type(exc).__name__}"
        try:
            neo_rows, neo_ms = fetch(neo, args.neo_db, neo_body, neo_params, args.timeout)
        except Exception as exc:  # noqa: BLE001
            note = f"NEO-ERR {type(exc).__name__}"
        if eg_rows is not None and neo_rows is not None and note == "MATCH":
            if sorted(eg_rows) != sorted(neo_rows):
                note = "DIFF"
            elif eg_rows != neo_rows and "ORDER BY" in body.upper():
                note = "ORDER-DIFF"
        if note != "MATCH":
            mismatches += 1
        print(f"{f.stem.replace('interactive-', ''):22} "
              f"{(len(eg_rows) if eg_rows is not None else -1):>8} "
              f"{(len(neo_rows) if neo_rows is not None else -1):>9} "
              f"{eg_ms:>8.0f} {neo_ms:>8.0f}  {note}")
        if note == "DIFF":
            only_eg = [r for r in eg_rows if r not in neo_rows][:3]
            only_neo = [r for r in neo_rows if r not in eg_rows][:3]
            print(f"{'':22}  仅在 eugraph: {only_eg}")
            print(f"{'':22}  仅在 neo4j  : {only_neo}")

    eg.close()
    neo.close()
    print("-" * 74)
    print(f"比对 {len(files)} 条查询，差异/错误 {mismatches} 条")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
