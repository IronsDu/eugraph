#!/usr/bin/env python3
"""
Extract openCypher TCK feature files and convert to JSON-based test cases.

Usage:
    python3 scripts/extract_tck.py \
        --tck /tmp/openCypher/tck \
        --out tests/correctness \
        --filter filter_config.json
"""

import argparse
import json
import os
import re
import sys
from pathlib import Path

try:
    from gherkin import Parser
except ImportError:
    print("ERROR: gherkin-official not installed. Run: pip install gherkin-official",
          file=sys.stderr)
    sys.exit(1)


# ── Feature → project capability mapping ──────────────────────────

# Categories of TCK features and what the project currently supports.
# Each entry maps a .feature file name pattern to one of:
#   "full"     — scenarios are directly extractable
#   "partial"  — some scenarios may need hand editing
#   "skip"     — skip entirely (not yet implemented)
#   "error"    — error scenarios only (verify correct error handling)

FEATURE_CAPABILITY = {
    # Expressions — comparison
    "Comparison": "partial",   # requires toInteger which we may not have

    # Expressions — boolean
    "Boolean": "partial",

    # Expressions — null
    "Null": "full",

    # Expressions — mathematical
    "Mathematical": "partial",

    # Expressions — string (mostly not implemented)
    "String": "skip",

    # Expressions — list (not implemented)
    "List": "skip",

    # Expressions — aggregation
    "Aggregation": "partial",

    # Expressions — type conversion (not implemented)
    "TypeConversion": "skip",

    # Expressions — temporal, conditional, literals
    "Temporal": "skip",
    "Conditional": "skip",
    "Literals": "full",

    # Expressions — other
    "ExistentialSubqueries": "skip",
    "Graph": "skip",
    "Pattern": "partial",
    "Precedence": "full",
    "Quantifier": "skip",

    # Clauses — MATCH
    "Match": "partial",

    # Clauses — WHERE
    "MatchWhere": "partial",

    # Clauses — RETURN
    "Return": "partial",

    # Clauses — ORDER BY / SKIP LIMIT
    "ReturnOrderBy": "partial",
    "ReturnSkipLimit": "partial",
    "WithOrderBy": "skip",
    "WithSkipLimit": "skip",

    # Clauses — WITH
    "With": "skip",
    "WithWhere": "skip",

    # Clauses — CREATE/DELETE/SET/REMOVE/MERGE
    "Create": "skip",
    "Delete": "skip",
    "Set": "skip",
    "Remove": "skip",
    "Merge": "skip",

    # Clauses — UNWIND
    "Unwind": "skip",

    # Clauses — UNION
    "Union": "skip",

    # Clauses — CALL
    "Call": "skip",

    # Use cases
    "TriadicSelection": "skip",
    "CountingSubgraphMatches": "skip",
}


def classify_feature(feature_name: str) -> str:
    """Determine capability level for a feature file by matching its name."""
    for pattern, capability in FEATURE_CAPABILITY.items():
        if feature_name.startswith(pattern):
            return capability
    return "skip"


# ── Gherkin AST helpers ────────────────────────────────────────────

def _get_doc_string(step: dict) -> str | None:
    """Extract doc string content from a step, if present."""
    if "docString" in step:
        return step["docString"]["content"].strip()
    return None


def _get_data_table(step: dict) -> list[list[str]] | None:
    """Extract data table rows as list of lists, or None."""
    if "dataTable" not in step:
        return None
    rows = []
    for row in step["dataTable"]["rows"]:
        rows.append([cell["value"] for cell in row["cells"]])
    return rows


def _expand_params(text: str, params: dict[str, str]) -> str:
    """Replace <param> placeholders in text with values from Examples table."""
    def _replace(m: re.Match) -> str:
        key = m.group(1)
        return params.get(key, m.group(0))
    return re.sub(r"<([^>]+)>", _replace, text)


# ── Scenario extraction ────────────────────────────────────────────

def extract_scenario(scenario: dict, feature_name: str) -> dict | None:
    """
    Extract a single Scenario (non-outline) into a test case dict.
    Returns None if the scenario cannot be converted.
    """
    steps = scenario["steps"]
    setup_statements: list[str] = []
    query: str | None = None
    expect_columns: list[str] = []
    expect_rows: list[list[str]] = []
    ordered: bool = False
    is_error: bool = False
    error_type: str | None = None
    error_contains: str | None = None
    requires: list[str] = []

    for step in steps:
        text = step["text"].strip()
        keyword_type = step.get("keywordType", "")
        doc = _get_doc_string(step)

        if "having executed:" in text and doc:
            setup_statements.extend(_split_setup_stmts(doc))

        elif keyword_type == "Action":
            if "executing query:" in text and doc:
                query = doc
                # Detect required features from the query
                requires = _detect_requires(query)
            elif "executing control query:" in text and doc:
                # Side-effect verification — skip for now
                pass

        elif keyword_type == "Outcome":
            if "result should be, in any order" in text:
                table = _get_data_table(step)
                if table:
                    expect_columns = table[0]
                    expect_rows = table[1:]
            elif "result should be, in order" in text:
                ordered = True
                table = _get_data_table(step)
                if table:
                    expect_columns = table[0]
                    expect_rows = table[1:]
            elif "result should be empty" in text:
                expect_columns = []
                expect_rows = []
            elif "SyntaxError should be raised" in text:
                is_error = True
                error_type = "SyntaxError"
                m = re.search(r"at compile time:\s*(.+)", text)
                if m:
                    error_contains = m.group(1).strip()
            elif "TypeError should be raised" in text:
                is_error = True
                error_type = "TypeError"
                m = re.search(r"at runtime:\s*(.+)", text)
                if m:
                    error_contains = m.group(1).strip()
            elif "error should be raised" in text:
                is_error = True
                m = re.search(r"at compile time:\s*(.+)", text)
                if m:
                    error_type = "SyntaxError"
                    error_contains = m.group(1).strip()
                else:
                    m = re.search(r"at runtime:\s*(.+)", text)
                    if m:
                        error_type = "TypeError"
                        error_contains = m.group(1).strip()

    if is_error:
        if query is None:
            return None
        return {
            "id": f"{feature_name}-{scenario.get('name', '?')}",
            "description": scenario.get("name", ""),
            "is_error": True,
            "error_type": error_type,
            "error_contains": error_contains,
            "setup": setup_statements,
            "query": query,
            "requires": requires,
            "tags": [t["name"] for t in scenario.get("tags", [])],
        }

    if query is None:
        return None

    return {
        "id": f"{feature_name}-{scenario.get('name', '?')}",
        "description": scenario.get("name", ""),
        "setup": setup_statements,
        "query": query,
        "expect_columns": expect_columns,
        "expect_rows": expect_rows,
        "ordered": ordered,
        "requires": requires,
        "tags": [t["name"] for t in scenario.get("tags", [])],
    }


def extract_scenario_outline(scenario: dict, feature_name: str) -> list[dict]:
    """Extract a Scenario Outline by expanding Examples tables."""
    steps = scenario["steps"]
    examples = scenario.get("examples", [])
    if not examples:
        return []

    results = []
    for ex in examples:
        if "tableHeader" not in ex:
            continue
        headers = [c["value"] for c in ex["tableHeader"]["cells"]]
        for row in ex["tableBody"]:
            values = [c["value"] for c in row["cells"]]
            params = dict(zip(headers, values))

            # Build scenario name from params
            param_desc = ", ".join(f"{k}={v}" for k, v in params.items())
            scenario_name = f"{scenario.get('name', '?')} [{param_desc}]"

            setup_statements: list[str] = []
            query: str | None = None
            expect_columns: list[str] = []
            expect_rows: list[list[str]] = []
            ordered: bool = False
            is_error: bool = False
            error_type: str | None = None
            error_contains: str | None = None
            requires: list[str] = []

            for step in steps:
                text = _expand_params(step["text"].strip(), params)
                keyword_type = step.get("keywordType", "")
                doc = _get_doc_string(step)

                if "having executed:" in text and doc:
                    setup_statements.extend(
                        _split_setup_stmts(_expand_params(doc, params)))

                elif keyword_type == "Action":
                    if "executing query:" in text and doc:
                        query = _expand_params(doc, params)
                        requires = _detect_requires(query)

                elif keyword_type == "Outcome":
                    if "result should be, in any order" in text:
                        table = _get_data_table(step)
                        if table:
                            expect_columns = [_expand_params(c, params)
                                              for c in table[0]]
                            expect_rows = [[_expand_params(c, params)
                                            for c in row]
                                           for row in table[1:]]
                    elif "result should be, in order" in text:
                        ordered = True
                        table = _get_data_table(step)
                        if table:
                            expect_columns = [_expand_params(c, params)
                                              for c in table[0]]
                            expect_rows = [[_expand_params(c, params)
                                            for c in row]
                                           for row in table[1:]]
                    elif "result should be empty" in text:
                        expect_columns = []
                        expect_rows = []
                    elif "SyntaxError should be raised" in text:
                        is_error = True
                        error_type = "SyntaxError"
                        m = re.search(r"at compile time:\s*(.+)", text)
                        if m:
                            error_contains = m.group(1).strip()
                    elif "TypeError should be raised" in text:
                        is_error = True
                        error_type = "TypeError"
                        m = re.search(r"at runtime:\s*(.+)", text)
                        if m:
                            error_contains = m.group(1).strip()

            if query is None:
                continue

            if is_error:
                results.append({
                    "id": f"{feature_name}-{scenario_name}",
                    "description": scenario_name,
                    "is_error": True,
                    "error_type": error_type,
                    "error_contains": error_contains,
                    "setup": setup_statements,
                    "query": query,
                    "requires": requires,
                    "tags": [t["name"] for t in scenario.get("tags", [])],
                })
            else:
                results.append({
                    "id": f"{feature_name}-{scenario_name}",
                    "description": scenario_name,
                    "setup": setup_statements,
                    "query": query,
                    "expect_columns": expect_columns,
                    "expect_rows": expect_rows,
                    "ordered": ordered,
                    "requires": requires,
                    "tags": [t["name"] for t in scenario.get("tags", [])],
                })

    return results


CYPHER_CLAUSE_KEYWORDS = {
    "CREATE", "MATCH", "OPTIONAL", "MERGE", "DELETE", "DETACH",
    "SET", "REMOVE", "RETURN", "WITH", "UNWIND", "CALL", "FOREACH",
    "UNION", "LOAD", "USING",
}


def _split_setup_stmts(doc: str) -> list[str]:
    """Split a doc string containing multiple Cypher statements into a list.

    Handles multi-line statements by joining continuation lines
    (lines that don't start with a Cypher keyword) with the previous statement.
    """
    raw_lines = doc.strip().split("\n")
    stmts = []
    current = []

    for line in raw_lines:
        stripped = line.strip()
        if not stripped:
            continue

        first_word = stripped.split()[0] if stripped.split() else ""
        if first_word in CYPHER_CLAUSE_KEYWORDS:
            if current:
                stmts.append(" ".join(current))
            current = [stripped]
        else:
            if current:
                current.append(stripped)
            else:
                # First line without keyword — treat as standalone
                current = [stripped]

    if current:
        stmts.append(" ".join(current))

    return stmts


def _detect_requires(query: str) -> list[str]:
    """Detect which Cypher features a query requires."""
    features = []
    checks = {
        "toInteger": "toInteger",
        "toFloat": "toFloat",
        "toBoolean": "toBoolean",
        "toString": "toString",
        "UNWIND": "UNWIND",
        "collect": "collect",
        "OPTIONAL MATCH": "OPTIONAL MATCH",
        "CASE": "CASE",
        "WHEN": "CASE",
        "DISTINCT": "DISTINCT",
        "ORDER BY": "ORDER BY",
        "SKIP": "SKIP",
        "LIMIT": "LIMIT",
        "IN ": "IN",
        "STARTS WITH": "STARTS WITH",
        "ENDS WITH": "ENDS WITH",
        "CONTAINS": "CONTAINS",
        "coalesce": "coalesce",
        "substring": "substring",
        "replace": "replace",
        "trim": "trim",
        "split": "split",
        "left": "left",
        "right": "right",
        "lTrim": "lTrim",
        "rTrim": "rTrim",
        "toUpper": "toUpper",
        "toLower": "toLower",
        "reverse": "reverse",
        "toString": "toString",
        "percentile": "percentile",
        "stDev": "stDev",
        "XOR": "XOR",
        "ALL(": "quantifier",
        "ANY(": "quantifier",
        "NONE(": "quantifier",
        "SINGLE(": "quantifier",
        "nodes(": "nodes",
        "relationships(": "relationships",
        "DETACH DELETE": "DETACH DELETE",
        "SET ": "SET",
        "REMOVE ": "REMOVE",
        "MERGE ": "MERGE",
        "UNION": "UNION",
        "CREATE ": "CREATE",
        "DELETE ": "DELETE",
    }
    for keyword, feature in checks.items():
        if keyword in query:
            features.append(feature)
    return sorted(set(features))


# ── Main extraction pipeline ───────────────────────────────────────

def extract_feature_file(filepath: Path) -> dict:
    """Parse a .feature file and return all extractable scenarios."""
    with open(filepath, "r", encoding="utf-8") as f:
        content = f.read()

    parser = Parser()
    try:
        ast = parser.parse(content)
    except Exception as e:
        print(f"  WARNING: parse error in {filepath.name}: {e}", file=sys.stderr)
        return {"feature": filepath.stem, "scenarios": [], "skipped": 0}

    feature = ast.get("feature", {})
    feature_name = feature.get("name", filepath.stem)
    children = feature.get("children", [])

    scenarios = []
    skipped = 0

    for child in children:
        if "scenario" not in child:
            continue

        scenario = child["scenario"]
        keyword = scenario.get("keyword", "")

        if keyword == "Scenario Outline":
            extracted = extract_scenario_outline(scenario, feature_name)
            for tc in extracted:
                if _should_skip(tc):
                    skipped += 1
                else:
                    scenarios.append(tc)
        elif keyword == "Scenario":
            tc = extract_scenario(scenario, feature_name)
            if tc is None:
                skipped += 1
            elif _should_skip(tc):
                skipped += 1
            else:
                scenarios.append(tc)

    return {
        "feature": feature_name,
        "source_file": str(filepath.relative_to(filepath.parent.parent.parent)),
        "scenarios": scenarios,
        "skipped": skipped,
    }


def _should_skip(tc: dict) -> bool:
    """Check if a test case should be skipped based on required features."""
    # Skip scenarios tagged as style checks
    if "skipStyleCheck" in tc.get("tags", []):
        # These test syntax case-insensitivity, keep them
        pass

    return False


def generate_cpp_registration(results: list[dict], output_path: Path):
    """Generate correctness_registration.cpp with GTest INSTANTIATE_TEST_SUITE_P calls.

    Each JSON file with extracted scenarios gets its own test suite, using the
    CorrectnessTest fixture and CorrectnessCase parameter type.
    The output is #included by test_correctness.cpp.
    """
    suites = []
    for result in sorted(results, key=lambda r: (r["category"], r["feature"])):
        if result["extracted"] == 0:
            continue
        cat = result["category"]
        name = result["feature"]
        rel_path = f"{cat}/{name}.json"
        # Sanitize for C++ identifier / GTest suite name
        suite_name = re.sub(r'[^a-zA-Z0-9]', '_', f"{cat}_{name}")
        suites.append((suite_name, rel_path, name, result["extracted"]))

    lines = [
        "// Auto-generated by scripts/extract_tck.py --gen-cpp",
        "// DO NOT EDIT MANUALLY",
        "//",
        f"// Generated {len(suites)} test suite registrations",
        "//",
        "",
        "#ifndef CORRECTNESS_JSON_DIR",
        '#define CORRECTNESS_JSON_DIR "tests/correctness"',
        "#endif",
        "",
    ]

    for suite_name, rel_path, feature_name, count in suites:
        var_name = f"__cases_{suite_name}"
        lines.append(
            f"// {feature_name} ({count} scenarios)\n"
            f"static auto {var_name} = "
            f"LoadCasesFromJSON("
            f"std::string(CORRECTNESS_JSON_DIR) + \"/{rel_path}\");\n"
            f"INSTANTIATE_TEST_SUITE_P(\n"
            f"    {suite_name},\n"
            f"    CorrectnessTest,\n"
            f"    ::testing::ValuesIn({var_name}));\n"
        )

    with open(output_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

    print(f"C++ registration written to {output_path} ({len(suites)} suites)")


def generate_markdown_summary(results: list[dict], output_dir: Path) -> str:
    """Generate a human-readable markdown summary of all extracted test cases."""
    lines = [
        "# TCK Correctness Test Cases",
        "",
        f"**Extracted**: {sum(r['extracted'] for r in results)} scenarios  ",
        f"**Skipped**: {sum(r['skipped'] for r in results)} scenarios  ",
        f"**Feature files**: {len(results)}  ",
        "**Source**: openCypher TCK (Apache 2.0)  ",
        "",
        "---",
        "",
    ]

    for result in results:
        name = result["feature"]
        n_extracted = result["extracted"]
        if n_extracted == 0:
            continue  # Skip features with no extracted scenarios

        n_skipped = result["skipped"]
        total = n_extracted + n_skipped

        lines.append(f"## {name} ({n_extracted}/{total} extracted)")
        lines.append("")

        if result["scenarios"]:
            lines.append("| # | ID | Type | Description |")
            lines.append("|---|-----|------|-------------|")
            for i, tc in enumerate(result["scenarios"], 1):
                ttype = "ERROR" if tc.get("is_error") else "QUERY"
                desc = tc.get("description", "")[:80]
                lines.append(f"| {i} | `{tc['id']}` | {ttype} | {desc} |")
            lines.append("")

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(
        description="Extract openCypher TCK feature files to JSON test cases")
    parser.add_argument("--tck", required=True,
                        help="Path to TCK root directory (e.g. /tmp/openCypher/tck)")
    parser.add_argument("--out", required=True,
                        help="Output directory for JSON test files")
    parser.add_argument("--features", default="all",
                        help="Comma-separated list of features to extract (default: all)")
    parser.add_argument("--gen-cpp", metavar="OUTPUT_PATH",
                        help="Generate C++ test registration file")
    parser.add_argument("--summary", action="store_true",
                        help="Generate a markdown summary file")
    args = parser.parse_args()

    tck_dir = Path(args.tck)
    features_dir = tck_dir / "features"
    if not features_dir.exists():
        print(f"ERROR: TCK features directory not found: {features_dir}",
              file=sys.stderr)
        sys.exit(1)

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    feature_files = sorted(features_dir.rglob("*.feature"))
    print(f"Found {len(feature_files)} feature files")

    # Filter features if specified
    if args.features != "all":
        requested = set(args.features.split(","))
        feature_files = [f for f in feature_files
                         if any(r in f.stem for r in requested)]

    results = []
    total_extracted = 0
    total_skipped = 0

    for fp in feature_files:
        rel_category = str(fp.parent.relative_to(features_dir))
        feature_stem = fp.stem
        capability = classify_feature(feature_stem)

        if capability == "skip":
            # Don't even parse — but count for reporting
            results.append({
                "feature": feature_stem,
                "category": rel_category,
                "scenarios": [],
                "extracted": 0,
                "skipped": 0,
                "capability": "skip",
            })
            continue

        print(f"  Processing {rel_category}/{fp.name} [{capability}]...", end=" ")

        parsed = extract_feature_file(fp)
        extracted = parsed["scenarios"]

        # For "partial" features, mark all scenarios with a note
        if capability == "partial":
            for tc in extracted:
                tc["note"] = "partial: may need manual adjustment"

        # Write JSON output
        json_out = out_dir / rel_category / f"{feature_stem}.json"
        json_out.parent.mkdir(parents=True, exist_ok=True)
        with open(json_out, "w", encoding="utf-8") as f:
            json.dump({
                "source": parsed["source_file"],
                "source_version": "openCypher TCK (Apache 2.0)",
                "feature": parsed["feature"],
                "capability": capability,
                "scenarios": extracted,
            }, f, indent=2, ensure_ascii=False)

        n_extracted = len(extracted)
        n_skipped = parsed["skipped"]

        print(f"{n_extracted} extracted, {n_skipped} skipped")

        results.append({
            "feature": feature_stem,
            "category": rel_category,
            "scenarios": extracted,
            "extracted": n_extracted,
            "skipped": n_skipped,
            "capability": capability,
        })
        total_extracted += n_extracted
        total_skipped += n_skipped

    # Write index
    index = {
        "total_feature_files": len(feature_files),
        "total_extracted": total_extracted,
        "total_skipped": total_skipped,
        "categories": {},
    }
    for r in results:
        cat = r["category"]
        if cat not in index["categories"]:
            index["categories"][cat] = {
                "extracted": 0,
                "skipped": 0,
                "features": [],
            }
        index["categories"][cat]["extracted"] += r["extracted"]
        index["categories"][cat]["skipped"] += r["skipped"]
        index["categories"][cat]["features"].append({
            "name": r["feature"],
            "extracted": r["extracted"],
            "skipped": r["skipped"],
            "capability": r["capability"],
        })

    with open(out_dir / "index.json", "w", encoding="utf-8") as f:
        json.dump(index, f, indent=2, ensure_ascii=False)

    # Generate markdown summary
    if args.summary:
        md = generate_markdown_summary(results, out_dir)
        with open(out_dir / "README.md", "w", encoding="utf-8") as f:
            f.write(md)
        print(f"\nSummary written to {out_dir / 'README.md'}")

    # Generate C++ registration
    if args.gen_cpp:
        generate_cpp_registration(results, Path(args.gen_cpp))

    print(f"\nDone. {total_extracted} scenarios extracted, "
          f"{total_skipped} skipped → {out_dir}")


if __name__ == "__main__":
    main()
