#!/usr/bin/env python3
"""Compare two TCK reports to find regressed scenarios and steps.

Uses difflib to align scenario headers, then compares steps within matched scenarios
to find steps that passed in baseline but failed in PR.
"""
import re
import sys
import difflib
from collections import defaultdict

def strip_ansi(text):
    return re.sub(r'\x1b\[[0-9;]*m', '', text)

def get_status(raw_line):
    if '38;2;000;128;000m' in raw_line: return 'passed'
    elif '38;2;255;000;000m' in raw_line: return 'failed'
    elif '38;2;255;255;000m' in raw_line: return 'undefined'
    elif '38;2;000;255;255m' in raw_line: return 'skipped'
    return None

STEP_RE = re.compile(r'^\s*[^\w\s]?\s*(\w+)\s+(.*?)\s+#\s+(\S+):\d+\s*$')
def is_step_line(clean):
    return bool(STEP_RE.match(clean))

def parse_report_full(filepath):
    """Parse a TCK report into list of scenarios with steps.

    Each scenario: (header_line, line_index, steps)
    steps: list of (step_type, step_text, status)
    """
    scenarios = []
    current_header = None
    current_header_idx = None
    current_steps = []

    with open(filepath, 'r') as f:
        for i, line in enumerate(f):
            raw = line.rstrip('\n')
            status = get_status(raw)
            clean = strip_ansi(raw).strip()

            if clean.startswith('Scenario:') or clean.startswith('Scenario Outline:'):
                if current_header and current_steps:
                    scenarios.append((current_header, current_header_idx, current_steps))
                # Match the header to extract file and name
                m = re.match(r'^(?:Scenario|Scenario Outline):\s*(.*?)\s+#\s+(\S+):\d+', clean)
                if m:
                    current_header = (m.group(2), m.group(1))
                else:
                    current_header = ('', clean)
                current_header_idx = i
                current_steps = []
                continue

            if status is not None and is_step_line(clean):
                m = STEP_RE.match(clean)
                if m:
                    current_steps.append((m.group(1), m.group(2).strip(), status))

    if current_header and current_steps:
        scenarios.append((current_header, current_header_idx, current_steps))

    return scenarios


def main():
    if len(sys.argv) != 3:
        print("Usage: compare_tck.py <baseline_report> <pr_report>")
        sys.exit(1)

    baseline_file = sys.argv[1]
    pr_file = sys.argv[2]

    bl = parse_report_full(baseline_file)
    pr = parse_report_full(pr_file)

    # Build key lists for alignment
    bl_keys = [s[0] for s in bl]  # (file, name)
    pr_keys = [s[0] for s in pr]

    # Align using difflib
    sm = difflib.SequenceMatcher(None, bl_keys, pr_keys)
    ops = sm.get_opcodes()

    print(f"Baseline: {len(bl)} scenarios with headers")
    print(f"PR: {len(pr)} scenarios with headers")
    print(f"\nScenarios WITHOUT headers (passed silently):")
    print(f"  Baseline: ~966 (based on official report)")
    print(f"  PR: ~941 (based on official report)")

    regressed_scenarios = []  # BL passed (no header) -> PR failed (has header)
    step_regressions = []     # Individual steps that changed

    for tag, i1, i2, j1, j2 in ops:
        if tag == 'equal':
            # Compare steps within matched scenarios
            for bl_idx, pr_idx in zip(range(i1, i2), range(j1, j2)):
                bl_header, bl_line, bl_steps = bl[bl_idx]
                pr_header, pr_line, pr_steps = pr[pr_idx]
                for si, (bl_type, bl_text, bl_status) in enumerate(bl_steps):
                    if si >= len(pr_steps):
                        break
                    pr_type, pr_text, pr_status = pr_steps[si]
                    if bl_type == pr_type and bl_text == pr_text:
                        if bl_status == 'passed' and pr_status == 'failed':
                            step_regressions.append({
                                'file': bl_header[0],
                                'scenario': bl_header[1],
                                'step_index': si,
                                'step_type': bl_type,
                                'step_text': bl_text,
                            })

        elif tag == 'insert':
            # Scenarios in PR that don't exist in BL -> these were "passed" in BL (no header)
            for pr_idx in range(j1, j2):
                pr_header, pr_line, pr_steps = pr[pr_idx]
                regressed_scenarios.append((pr_header, pr_steps))

        elif tag == 'delete':
            # Scenarios in BL that don't exist in PR -> these now pass in PR (no header)
            pass

        elif tag == 'replace':
            # Scenarios changed between BL and PR
            for pr_idx in range(j1, j2):
                pr_header, pr_line, pr_steps = pr[pr_idx]
                regressed_scenarios.append((pr_header, pr_steps))

    print(f"\n{'='*80}")
    print(f"REGRESSED SCENARIOS (passed in baseline, FAILED in PR): {len(regressed_scenarios)}")
    print(f"STEP REGRESSIONS within matched scenarios: {len(step_regressions)}")
    print(f"{'='*80}")

    if regressed_scenarios:
        print(f"\n{'─'*80}")
        print(f"DETAILS OF REGRESSED SCENARIOS")
        print(f"{'─'*80}")

        by_file = defaultdict(list)
        for header, steps in regressed_scenarios:
            by_file[header[0]].append((header[1], steps))

        for fname in sorted(by_file):
            items = by_file[fname]
            print(f"\n📄 {fname} ({len(items)} scenario(s))")
            for name, steps in items:
                display_name = name[:120] + '...' if len(name) > 120 else name
                print(f"\n  Scenario: {display_name}")
                print(f"  Steps:")
                for si, (stype, stext, sstatus) in enumerate(steps):
                    status_mark = {'passed': '✔', 'failed': '✘', 'skipped': '↷', 'undefined': '■'}.get(sstatus, '?')
                    print(f"    [{si}] {status_mark} {stype} {stext[:130]}")

    if step_regressions:
        print(f"\n{'─'*80}")
        print(f"STEP REGRESSIONS (within unchanged scenarios)")
        print(f"{'─'*80}")
        for r in step_regressions:
            print(f"\n  File: {r['file']}")
            print(f"  Scenario: {r['scenario'][:120]}")
            print(f"  Step [{r['step_index']}]: {r['step_type']} {r['step_text'][:130]}")

    print(f"\n{'='*80}")
    print(f"SUMMARY")
    print(f"{'='*80}")
    print(f"  Regressed scenarios (was passed, now failed): {len(regressed_scenarios)}")
    print(f"  Step regressions within matched scenarios: {len(step_regressions)}")
    print(f"  Total: {len(regressed_scenarios) + len(step_regressions)}")

if __name__ == '__main__':
    main()
