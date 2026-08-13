#!/usr/bin/env python3
"""Generate a markdown unit-test summary from CTest's JUnit XML output.

CTest is invoked with ``--output-junit``, for example::

    ctest --preset=release --output-junit build/release/junit.xml

This script intentionally excludes the TCK and Bolt driver integration tests,
which are already reported separately by the TCK report workflow.  Everything
else in the JUnit file is treated as part of the unit-test suite.
"""

from __future__ import annotations

import argparse
import sys
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict
from pathlib import Path


EXCLUDED_TEST_NAMES = {
    "tck_tests",
    "bolt_python_driver_integration_tests",
    "bolt_js_ws_driver_integration_tests",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="Path to CTest JUnit XML file")
    parser.add_argument("output", type=Path, help="Path to write the markdown report")
    return parser.parse_args()


def first_text(elem: ET.Element | None) -> str:
    if elem is None:
        return ""
    value = elem.get("message") or elem.text or ""
    return value.strip()


def is_failed(testcase: ET.Element) -> bool:
    return testcase.find("failure") is not None or testcase.find("error") is not None


def is_skipped(testcase: ET.Element) -> bool:
    status = (testcase.get("status") or "").lower()
    return testcase.find("skipped") is not None or status in {"notrun", "skipped", "disabled"}


def suite_name(testcase: ET.Element) -> str:
    name = testcase.get("name") or testcase.get("classname") or "unknown"
    return name.split(".", 1)[0] if "." in name else name


def build_report(junit_path: Path) -> str:
    root = ET.parse(junit_path).getroot()
    testcases = list(root.iter("testcase"))

    rows = []
    failed_rows = []
    skipped_rows = []
    for testcase in testcases:
        name = testcase.get("name") or testcase.get("classname") or "unknown"
        if name in EXCLUDED_TEST_NAMES:
            continue
        if is_failed(testcase):
            failed_rows.append((name, first_text(testcase.find("failure")) or first_text(testcase.find("error"))))
        elif is_skipped(testcase):
            skipped_rows.append(name)
        else:
            rows.append((name, suite_name(testcase)))

    total = len(rows) + len(failed_rows) + len(skipped_rows)
    passed = len(rows)
    failed = len(failed_rows)
    skipped = len(skipped_rows)

    lines = ["## Unit Test Report", ""]

    if total == 0:
        lines.append("No unit test results were available.")
        return "\n".join(lines) + "\n"

    lines.extend(
        [
            "| Metric | Value |",
            "|---|---|",
            f"| Total | {total} |",
            f"| Passed | {passed} |",
            f"| Failed | {failed} |",
            f"| Skipped | {skipped} |",
            "",
        ]
    )

    suite_stats: dict[str, Counter] = defaultdict(Counter)
    for name, suite in rows:
        suite_stats[suite]["total"] += 1
        suite_stats[suite]["passed"] += 1
    for name, _ in failed_rows:
        suite = suite_name_from_full_name(name)
        suite_stats[suite]["total"] += 1
        suite_stats[suite]["failed"] += 1
    for name in skipped_rows:
        suite = suite_name_from_full_name(name)
        suite_stats[suite]["total"] += 1
        suite_stats[suite]["skipped"] += 1

    if suite_stats:
        lines.extend(
            [
                "### Suites",
                "",
                "| Suite | Total | Passed | Failed | Skipped |",
                "|---|---|---|---|---|",
            ]
        )
        for suite in sorted(suite_stats):
            stats = suite_stats[suite]
            lines.append(
                f"| {suite} | {stats['total']} | {stats['passed']} | "
                f"{stats['failed']} | {stats['skipped']} |"
            )
        lines.append("")

    if failed_rows:
        lines.extend(
            [
                "### Failed Tests",
                "",
                "| Test | Message |",
                "|---|---|",
            ]
        )
        shown = 0
        for name, message in failed_rows:
            if shown >= 50:
                break
            clean_message = message.replace("\n", " ").replace("|", "\\|").strip()
            lines.append(f"| `{name}` | {clean_message} |")
            shown += 1
        if failed > shown:
            lines.append(f"| ... | {failed - shown} more failures omitted |")
        lines.append("")

    return "\n".join(lines) + "\n"


def suite_name_from_full_name(name: str) -> str:
    return name.split(".", 1)[0] if "." in name else name


def main() -> None:
    args = parse_args()
    if not args.input.is_file():
        print(f"JUnit file not found: {args.input}", file=sys.stderr)
        sys.exit(1)
    report = build_report(args.input)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(report, encoding="utf-8")
    print(f"Wrote {args.output}")


if __name__ == "__main__":
    main()
