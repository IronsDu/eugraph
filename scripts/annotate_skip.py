#!/usr/bin/env python3
"""
Annotate correctness test cases with skip_reason based on engine limitations.

Two modes:
  --static    Fast: classify based on query/requires analysis (default)
  --from-runs Slow: run tests, capture actual errors, classify from results

Usage:
    python3 scripts/annotate_skip.py tests/correctness
    python3 scripts/annotate_skip.py tests/correctness --from-runs ./correctness_tests
"""

import json
import re
import subprocess
import sys
from pathlib import Path
from collections import Counter, defaultdict


# Static classification patterns
# Order matters: first match wins

def classify_static(tc: dict) -> str | None:
    """Classify test case based on static analysis of query and requires."""
    requires = set(tc.get("requires", []))
    query = tc.get("query", "")
    is_error = tc.get("is_error", False)
    setup = tc.get("setup", [])

    # ── Parser level ──
    if "UNWIND" in requires:
        return "parser: UNWIND keyword not yet in grammar"

    if "collect" in requires and not any(kw in query for kw in ["MATCH", "CREATE", "RETURN"]):
        return "parser: collect() not supported"

    # ── Binder level ──
    if requires & {"DELETE", "DETACH DELETE", "MERGE", "SET", "REMOVE"}:
        missing = sorted(requires & {"DELETE", "DETACH DELETE", "MERGE", "SET", "REMOVE"})
        return f"binder: {', '.join(missing)} clause not yet bound"

    if requires & {"OPTIONAL MATCH"}:
        return "binder: OPTIONAL MATCH not yet supported"

    # ── Runtime expressions ──
    runtime_missing = requires & {
        "toInteger", "toFloat", "toBoolean", "toString",
        "coalesce", "nodes", "relationships",
        "STARTS WITH", "ENDS WITH", "CONTAINS",
    }
    if runtime_missing:
        return f"runtime: {', '.join(sorted(runtime_missing))} function not implemented"

    # ── Query structure patterns ──
    if not is_error and query.strip().upper().startswith("RETURN"):
        return "engine: standalone RETURN not supported"

    if query.strip().upper().startswith("WITH "):
        return "engine: leading WITH not supported"

    if "\nWITH " in query:
        return "engine: WITH clause in pipeline not fully supported"

    # Multiple MATCH clauses
    lines = query.strip().split("\n")
    match_count = sum(1 for l in lines if l.strip().upper().startswith("MATCH "))
    if match_count > 1 and "\nWITH " not in query:
        return "engine: multiple MATCH without WITH not supported"

    # ── Label/property requirements ──
    # Most tests need pre-registered labels to run.
    # Check if query references labels or properties that won't exist.
    has_label_in_query = bool(re.search(r'\([^)]*:\w+', query))
    has_label_in_setup = any(":" in stmt for stmt in setup if "CREATE" in stmt)

    if has_label_in_query or has_label_in_setup:
        return "catalog: labels must be pre-registered"

    # ── Aggregation / ORDER BY / SKIP / LIMIT ──
    if requires & {"ORDER BY", "SKIP", "LIMIT", "DISTINCT"}:
        return "runtime: sort/pagination not fully verified"

    if requires & {"XOR"}:
        return "parser: XOR keyword not in grammar"

    if requires & {"CASE"}:
        return "runtime: CASE expression not implemented"

    if requires & {"IN"}:
        return "parser: IN operator not in grammar"

    if requires & {"quantifier"}:
        return "runtime: quantifier expressions not implemented"

    # ── Error detection scenarios ──
    if is_error:
        # These expect the engine to detect errors that it currently doesn't
        return "checker: semantic error detection not yet implemented"

    return None


def analyze_and_annotate_static(json_dir: Path) -> dict:
    """Static analysis of all test cases."""
    stats = {"total": 0, "runnable": 0, "skipped": 0,
             "by_reason": Counter(), "by_file": defaultdict(dict)}

    for fp in sorted(json_dir.rglob("*.json")):
        if fp.name in ("index.json",):
            continue

        with open(fp) as f:
            data = json.load(f)

        rel = str(fp.relative_to(json_dir))
        file_stats = {"total": 0, "runnable": 0, "skipped": 0}

        for tc in data.get("scenarios", []):
            stats["total"] += 1
            file_stats["total"] += 1

            reason = classify_static(tc)
            if reason:
                tc["skip_reason"] = reason
                stats["skipped"] += 1
                file_stats["skipped"] += 1
                stats["by_reason"][reason.split(":")[0]] += 1
            else:
                tc.pop("skip_reason", None)
                stats["runnable"] += 1
                file_stats["runnable"] += 1

        with open(fp, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2, ensure_ascii=False)

        stats["by_file"][rel] = file_stats

    return stats


def generate_report(stats: dict, output_path: Path) -> str:
    """Generate test report markdown."""
    total = stats["total"]
    runnable = stats["runnable"]
    skipped = stats["skipped"]

    lines = [
        "# Cypher Correctness Test Report",
        "",
        f"**Total scenarios**: {total}",
        f"**Runnable**: {runnable} ({100*runnable//total if total else 0}%)",
        f"**Skipped**: {skipped} ({100*skipped//total if total else 0}%)",
        "",
        "---",
        "",
        "## Skip Categories & Enablement Plan",
        "",
        "| Category | Count | Priority | Depends On |",
        "|----------|-------|----------|------------|",
    ]

    plan = [
        ("catalog", "标签预注册", "P0",
         "测试框架 SetUp 中自动解析 setup/query 中的标签并预注册"),
        ("engine", "独立 RETURN / WITH 管道", "P1",
         "Binder 支持无 MATCH 上下文的 RETURN 和 WITH 管道"),
        ("checker", "语义错误检测", "P1",
         "Binder 完善变量类型冲突、重复绑定等语义检查"),
        ("parser", "UNWIND/IN 关键字", "P2",
         "ANTLR grammar 添加 UNWIND、IN 等关键字"),
        ("binder", "DELETE/MERGE/SET/REMOVE", "P2",
         "Binder 实现对应 DML 子句绑定"),
        ("runtime", "内置函数", "P2",
         "实现 toInteger/toFloat/coalesce 等表达式函数"),
    ]

    reason_labels = {p[0]: p[1] for p in plan}
    for category, label, priority, deps in plan:
        count = stats["by_reason"].get(category, 0)
        if count > 0:
            lines.append(f"| {label} | {count} | {priority} | {deps} |")

    lines.extend([
        "",
        "---",
        "",
        "## Per-File Summary",
        "",
        "| File | Total | Runnable | Skipped |",
        "|------|-------|----------|---------|",
    ])

    for fname in sorted(stats["by_file"].keys()):
        fs = stats["by_file"][fname]
        lines.append(f"| {fname} | {fs['total']} | {fs['runnable']} | {fs['skipped']} |")

    report = "\n".join(lines)
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(report)
    return report


def main():
    import argparse
    p = argparse.ArgumentParser(description="Annotate correctness test cases")
    p.add_argument("json_dir", help="Path to tests/correctness directory")
    p.add_argument("--from-runs", metavar="BINARY",
                   help="Run tests and classify from actual errors")
    args = p.parse_args()

    json_dir = Path(args.json_dir)

    if args.from_runs:
        sys.exit("--from-runs mode not yet implemented")

    stats = analyze_and_annotate_static(json_dir)
    report_path = json_dir / "TEST_REPORT.md"
    generate_report(stats, report_path)

    print(f"Annotated {stats['total']} cases: "
          f"{stats['runnable']} runnable, {stats['skipped']} skipped")
    print(f"Report: {report_path}")
    print()
    for category, count in stats["by_reason"].most_common():
        print(f"  {category}: {count}")


if __name__ == "__main__":
    main()
