#!/usr/bin/env python3
"""Generate detailed TCK test results report in markdown format.

Usage:
  # Step 1: Run TCK to get step-results JSON
  python3 tests/tck/run_tck.py
      --server-bin BUILD/eugraph-server
      --tck-bin BUILD/tests/tck/tck_tests
      --features BUILD/tests/tck/features
      --step-results-file BUILD/tck-step-results.json

  # Step 2: Generate report
  python3 scripts/gen_tck_report.py
      --step-results BUILD/tck-step-results.json
      --features BUILD/tests/tck/features
      --output docs/tests/tck-results.md

  # One-liner:
  python3 tests/tck/run_tck.py ... --step-results-file BUILD/tck-step-results.json \\
      && python3 scripts/gen_tck_report.py --step-results BUILD/tck-step-results.json \\
         --features BUILD/tests/tck/features --output docs/tests/tck-results.md

The report includes:
  - Overall pass/fail counts (scenarios + steps)
  - Per-category breakdown (expressions: List, Aggregation, Pattern, etc.)
  - Per-category breakdown (clauses: Match, WITH, MERGE, Create, etc.)
  - useCases section
  - Remaining issues summary

Requires: Python 3.6+
"""

import argparse
import json
import os
import glob
import re
import subprocess
import sys


def count_feat(prefix, scenario_to_feature, scenario_status, scenario_skip,
               ast_skipped_scenarios=None):
    """Count (total, passed, failed, skipped) scenarios for a feature prefix.
    Skipped includes query-execution-never-reached and AST-skipped scenarios."""
    scs = set()
    for sc, status in scenario_status.items():
        feat = scenario_to_feature.get(sc, '')
        if feat.startswith(prefix):
            scs.add(sc)
    # Also include AST-skipped scenarios
    if ast_skipped_scenarios:
        for sc in ast_skipped_scenarios:
            feat = scenario_to_feature.get(sc, '')
            if feat.startswith(prefix):
                scs.add(sc)
    passed = 0
    failed = 0
    skipped = 0
    for sc in scs:
        if scenario_status.get(sc) == 'FAILED':
            failed += 1
        elif sc in scenario_skip or (ast_skipped_scenarios and sc in ast_skipped_scenarios):
            skipped += 1
        else:
            passed += 1
    return len(scs), passed, failed, skipped


def count_multi(prefixes, scenario_to_feature, scenario_status, scenario_skip,
                ast_skipped_scenarios=None):
    total = passed = failed = skipped = 0
    for p in prefixes:
        t, p, f, s = count_feat(p, scenario_to_feature, scenario_status, scenario_skip,
                                ast_skipped_scenarios)
        total += t
        passed += p
        failed += f
        skipped += s
    return total, passed, failed, skipped


def build_scenario_map(step_data, feature_dir):
    """Map step result scenario names to feature file paths using the 'feature'
    field embedded in JSON by tck_steps.cpp. Converts absolute paths to
    relative (vs feature_dir) for prefix matching."""
    scenario_to_feature = {}
    for step in step_data:
        sc = step['scenario']
        feat = step.get('feature', '')
        if feat:
            # Normalize to relative path vs feature_dir
            if os.path.isabs(feat):
                feat = os.path.relpath(feat, feature_dir)
            scenario_to_feature[sc] = feat
    return scenario_to_feature


def main():
    parser = argparse.ArgumentParser(
        description="Generate detailed TCK test results report in markdown")
    parser.add_argument("--step-results", required=True,
                        help="Path to tck-step-results.json (from run_tck.py)")
    parser.add_argument("--features", required=True,
                        help="Path to TCK features directory")
    parser.add_argument("--output", default=None,
                        help="Output markdown file (default: stdout)")
    args = parser.parse_args()

    with open(args.step_results) as f:
        step_data = json.load(f)

    feature_dir = args.features
    scenario_to_feature = build_scenario_map(step_data, feature_dir)

    # Scenario pass/fail/skip status
    scenario_status = {}
    scenario_skip = set()  # scenarios where the query itself was skipped
    scenario_has_query = set()  # scenarios that actually executed a query
    for step in step_data:
        sc = step['scenario']
        if sc not in scenario_status:
            scenario_status[sc] = 'PASSED'
        if step['status'] == 'FAILED':
            scenario_status[sc] = 'FAILED'
        if step['status'] == 'SKIPPED' and 'executing query' in step['step']:
            scenario_skip.add(sc)
        if 'executing query' in step['step']:
            scenario_has_query.add(sc)

    # Scenarios that never executed a query (undefined step not recorded in
    # JSON, or all query-execution steps were SKIPPED before reaching query)
    # are counted as skipped, not passed.
    for sc in list(scenario_status.keys()):
        if sc not in scenario_has_query and sc not in scenario_skip:
            scenario_skip.add(sc)

    # AST-skipped scenarios: in feature files but not in step data at all.
    # Step data names may include [ex #N] suffixes for Scenario Outline
    # example rows; strip them before comparing with feature file names.
    all_file_scenarios = {}
    for fpath in sorted(glob.glob(f'{feature_dir}/**/*.feature', recursive=True)):
        rel = os.path.relpath(fpath, feature_dir)
        with open(fpath) as ff:
            for line in ff:
                m = re.match(r'\s*Scenario(?: Outline)?:\s*\[(\d+)\]\s*(.+)',
                             line.strip())
                if m:
                    raw = re.sub(r'\s+', ' ', m.group(2).strip())
                    sc_name = f'[{m.group(1)}] {raw}'
                    all_file_scenarios[sc_name] = rel
    base_names = set()
    for s in step_data:
        base = re.sub(r' \[ex #\d+\]$', '', s['scenario'])
        base_names.add(base)
    ast_skipped_scenarios = set()
    for sc, rel in all_file_scenarios.items():
        if sc not in base_names:
            ast_skipped_scenarios.add(sc)
            scenario_to_feature[sc] = rel

    # Overall stats
    num_scenarios = len(scenario_status)
    passed_sc = 0
    failed_sc = 0
    skipped_sc = 0
    for sc in scenario_status:
        if scenario_status[sc] == 'FAILED':
            failed_sc += 1
        elif sc in scenario_skip:
            skipped_sc += 1
        else:
            passed_sc += 1
    ast_skipped_sc = len(ast_skipped_scenarios)
    total_steps = len(step_data)
    total_passed = sum(1 for s in step_data if s['status'] == 'PASSED')
    total_failed = sum(1 for s in step_data if s['status'] == 'FAILED')
    total_skipped = sum(1 for s in step_data if s['status'] == 'SKIPPED')
    total_undefined = sum(1 for s in step_data if s['status'] == 'UNDEFINED')

    cf = count_feat
    cm = count_multi
    sf = scenario_to_feature
    ss = scenario_status
    sk = scenario_skip
    ax = ast_skipped_scenarios

    # Build lines
    out = []
    w = out.append

    w("# TCK 测试结果报告")
    w("")
    git_date = subprocess.check_output(
        ['git', 'log', '-1', '--format=%cd', '--date=short'],
        text=True).strip()
    git_branch = subprocess.check_output(
        ['git', 'rev-parse', '--abbrev-ref', 'HEAD'],
        text=True).strip()
    w(f"**日期**: {git_date}")
    w(f"**分支**: {git_branch}")
    w(f"**构建**: build/tests/tck/tck_tests")
    w("")
    w("---")
    w("")
    w("## 总体结果")
    w("")
    w("| 指标 | 数量 |")
    w("|------|------|")
    w(f"| 场景总数 | {num_scenarios + ast_skipped_sc} |")
    w(f"| └ 已执行 | {num_scenarios} |")
    w(f"| 场景通过 | **{passed_sc}** |")
    w(f"| 场景失败 | {failed_sc} |")
    w(f"| 场景跳过/未定义 | {skipped_sc} |")
    w(f"| AST跳过（未执行） | {ast_skipped_sc} |")
    w(f"| 步骤总数 | {total_steps} |")
    w(f"| 步骤通过 | {total_passed} |")
    w(f"| 步骤失败 | {total_failed} |")
    w(f"| 步骤跳过 | {total_skipped} |")
    w("")
    w("---")
    w("")
    w("## 表达式类")
    w("")
    w("| 类别 | 场景数 | 通过 | 失败 | 跳过 | 失败率 | 主要问题 |")
    w("|------|--------|------|------|------|--------|---------|")

    exp_cats = [
        ("Literals", "expressions/literals", ""),
        ("Conditional", "expressions/conditional", ""),
        ("Boolean", "expressions/boolean", ""),
        ("Map", "expressions/map", ""),
        ("Null", "expressions/null", ""),
        ("Path", "expressions/path", ""),
        ("Mathematical", "expressions/mathematical", ""),
        ("String", "expressions/string", ""),
        ("Precedence", "expressions/precedence", ""),
        ("Temporal", "expressions/temporal", ""),
        ("Graph", "expressions/graph", ""),
        ("Quantifier", "expressions/quantifier", ""),
        ("List", "expressions/list", "Pattern comprehension 未实现"),
        ("TypeConversion", "expressions/typeConversion", "跨积 MATCH 预存问题"),
        ("Comparison", "expressions/comparison", ""),
        ("Pattern", "expressions/pattern", "模式子查询未完善"),
        ("Aggregation", "expressions/aggregation",
         "percentileDisc/Cont 函数未实现"),
        ("ExistentialSubqueries", "expressions/existentialSubqueries",
         "EXISTS 子查询"),
    ]

    # Pre-compute and sort: all-passed first, then by failure rate descending
    def compute_exp_row(name, prefix, prob):
        t, p, f, s = cf(prefix, sf, ss, sk, ax)
        if t == 0:
            return None
        fr = f / t if t > 0 else 0.0
        return (name, t, p, f, s, fr, prob)

    exp_rows = [row for row in (compute_exp_row(n, pr, pb) for n, pr, pb in exp_cats) if row]
    exp_rows.sort(key=lambda r: (0 if r[3] == 0 and r[4] == 0 else 1, -r[5]))

    for name, t, p, f, s, fr, prob in exp_rows:
        rate = f"{fr*100:.1f}%"
        emoji = "✅" if f == 0 and s == 0 else ""
        desc = prob if f > 0 else (f"{s} skipped" if s > 0 else emoji)
        w(f"| {name} | {t} | **{p}** | {f} | {s} | {rate} | {desc} |")

    w("")
    w("## 子句类")
    w("")
    w("| 类别 | 场景数 | 通过 | 失败 | 跳过 | 失败率 | 主要问题 |")
    w("|------|--------|------|------|------|--------|---------|")

    clause_cats = [
        ("Match", [f"clauses/match/Match{i}" for i in range(1, 11)]
                  + ["clauses/match-where/MatchWhere1", "clauses/match-where/MatchWhere6"],
         "变量作用域、路径模式、OPTIONAL MATCH"),
        ("WITH", ["clauses/with/With1", "clauses/with/With2",
                   "clauses/with/With4", "clauses/with/With6",
                   "clauses/with/With7"],
         "变量遮蔽、DISTINCT、WHERE"),
        ("WITH ORDER BY", ["clauses/with-orderBy/WithOrderBy1",
                            "clauses/with-orderBy/WithOrderBy2",
                            "clauses/with-orderBy/WithOrderBy3",
                            "clauses/with-orderBy/WithOrderBy4",
                            "clauses/with-orderBy/WithOrderBy5"],
         "ORDER BY 变量遮蔽、别名解析"),
        ("WITH WHERE", ["clauses/with-where/WithWhere1",
                         "clauses/with-where/WithWhere2",
                         "clauses/with-where/WithWhere4"],
         "WHERE子句变量解析"),
        ("WITH Skip Limit", ["clauses/with-skip-limit/WithSkipLimit1",
                              "clauses/with-skip-limit/WithSkipLimit2"],
         "SKIP/LIMIT与WITH的交互"),
        ("Create", [f"clauses/create/Create{i}" for i in range(1, 7)],
         "创建模式副作用计数"),
        ("MERGE", [f"clauses/merge/Merge{i}" for i in range(1, 10)],
         "ON MATCH/CREATE副作用、路径变量、属性比较"),
        ("RETURN", [f"clauses/return/Return{i}" for i in range(1, 9)],
         "列名生成、投影、DISTINCT"),
        ("RETURN ORDER BY", [f"clauses/return-orderby/ReturnOrderBy{i}"
                              for i in range(1, 7)],
         "ORDER BY表达式格式"),
        ("RETURN Skip Limit", ["clauses/return-skip-limit/ReturnSkipLimit1",
                                "clauses/return-skip-limit/ReturnSkipLimit2"],
         "SKIP/LIMIT参数类型"),
        ("Set", [f"clauses/set/Set{i}" for i in range(1, 7)],
         "SET属性/标签副作用计数"),
        ("Remove", [f"clauses/remove/Remove{i}" for i in range(1, 4)],
         "REMOVE副作用"),
        ("Delete", [f"clauses/delete/Delete{i}" for i in range(1, 7)],
         "表达式DELETE、路径DELETE、无向匹配"),
        ("Unwind", ["clauses/unwind/Unwind1"],
         "UNWIND类型边界"),
        ("Union", ["clauses/union/Union1"],
         "UNION类型对齐"),
        ("CALL", [f"clauses/call/Call{i}" for i in range(1, 7)],
         "CALL子句未实现（远期目标）"),
    ]

    def compute_clause_row(name, prefixes, prob):
        t, p, f, s = cm(prefixes, sf, ss, sk, ax)
        if t == 0:
            return None
        fr = f / t if t > 0 else 0.0
        return (name, t, p, f, s, fr, prob)

    clause_rows = [row for row in (compute_clause_row(n, pr, pb) for n, pr, pb in clause_cats) if row]
    clause_rows.sort(key=lambda r: (0 if r[3] == 0 and r[4] == 0 else 1, -r[5]))

    for name, t, p, f, s, fr, prob in clause_rows:
        rate = f"{fr*100:.1f}%"
        emoji = "✅" if f == 0 and s == 0 else ""
        desc = prob if (f > 0 or s > 0) else emoji
        w(f"| {name} | {t} | **{p}** | {f} | {s} | {rate} | {desc} |")

    w("")
    w("## useCases")
    w("")
    w("| 类别 | 场景数 | 通过 | 失败 | 未定义 |")
    w("|------|--------|------|------|--------|")
    for uc_name in ["countingSubgraphMatches", "triadicSelection"]:
        t, p, f, s = cf(f"useCases/{uc_name}", sf, ss, sk)
        undef = sum(1 for s in step_data
                    if s['status'] == 'UNDEFINED'
                    and uc_name in scenario_to_feature.get(s['scenario'], ''))
        w(f"| {uc_name} | {t} | **{p}** | {f} | {undef} |")

    w("")
    w("---")
    w("")
    w("## 剩余主要问题")
    w("")
    w("1. **WITH ORDER BY 排序表达式**（~40+ step failures）：percentileDisc/Cont、"
      "多列排序、list排序、类型一致性排序")
    w("2. **路径展示**（Match6 19 scenarios）：路径序列化格式未包含正确标签/属性/关系类型")
    w("3. **WITH 变量遮蔽/DISTINCT**（~15 scenarios）：别名重定义、WHERE子句变量解析")
    w("4. **Pattern Comprehension / EXISTS**（~30 scenarios）：特性未完整实现")
    w("5. **CREATE/SET/DELETE 副作用计数**：副作用追踪器细节")
    w("6. **Match 可变长/无向模式**（~60 scenarios）：OPTIONAL MATCH bound node、无向固定长路径")

    result = "\n".join(out) + "\n"

    if args.output:
        with open(args.output, 'w') as f:
            f.write(result)
        print(f"Report written to {args.output}")
    else:
        sys.stdout.write(result)


if __name__ == "__main__":
    main()
