#!/usr/bin/env python3
"""TCK test runner: start eugraph-server, run tck_tests, stop server.

Usage:
  run_tck.py --server-bin PATH --tck-bin PATH [OPTIONS] [--] [extra tck_tests args...]
"""

import argparse
import json
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import time
from collections import Counter


def tcp_probe(host: str, port: int, timeout_s: float = 1.0) -> bool:
    """Check if a TCP port is accepting connections."""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout_s)
        result = sock.connect_ex((host, port))
        sock.close()
        return result == 0
    except OSError:
        return False


def wait_for_server(host: str, port: int, timeout: int, pid: int) -> bool:
    """Poll until server is ready or process dies or timeout expires."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            wpid, status = os.waitpid(pid, os.WNOHANG)
            if wpid == pid:
                if os.WIFEXITED(status):
                    print(f"[run_tck] Server exited early with code {os.WEXITSTATUS(status)}",
                          file=sys.stderr)
                elif os.WIFSIGNALED(status):
                    print(f"[run_tck] Server killed by signal {os.WTERMSIG(status)}",
                          file=sys.stderr)
                return False
        except ChildProcessError:
            return False

        if tcp_probe(host, port, timeout_s=1.0):
            return True

        time.sleep(0.2)

    print(f"[run_tck] Server startup timed out after {timeout}s", file=sys.stderr)
    return False


def stop_server(pid: int) -> int:
    """Stop server gracefully, return exit status or -signal."""
    try:
        os.kill(pid, signal.SIGTERM)
    except ProcessLookupError:
        return 0

    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        try:
            wpid, status = os.waitpid(pid, os.WNOHANG)
            if wpid == pid:
                if os.WIFEXITED(status):
                    print(f"[run_tck] Server exited with code {os.WEXITSTATUS(status)}",
                          file=sys.stderr)
                    return os.WEXITSTATUS(status)
                elif os.WIFSIGNALED(status):
                    sig = os.WTERMSIG(status)
                    if sig == signal.SIGTERM:
                        print(f"[run_tck] Server stopped (SIGTERM)", file=sys.stderr)
                        return 0
                    print(f"[run_tck] Server killed by signal {sig}",
                          file=sys.stderr)
                    return -sig
        except ChildProcessError:
            return 0
        time.sleep(0.1)

    print("[run_tck] Server did not stop, sending SIGKILL", file=sys.stderr)
    try:
        os.kill(pid, signal.SIGKILL)
        wpid, status = os.waitpid(pid, 0)
        return -signal.SIGKILL
    except ProcessLookupError:
        return 0


def strip_ansi(text: str) -> str:
    """Remove ANSI escape sequences from text."""
    return re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', text)


def parse_skip_reasons(stderr_text: str) -> Counter:
    """Extract [TCK] skipping: reason counts from stderr."""
    counter = Counter()
    for line in stderr_text.splitlines():
        m = re.search(r'\[TCK\] skipping: (.+)$', line)
        if m:
            counter[m.group(1).strip()] += 1
    return counter


def parse_cucumber_summary(stdout_text: str):
    """Parse cucumber summary lines for scenarios and steps."""
    scenarios = None
    steps = None

    # Scenarios: "N scenarios X passed, Y undefined, Z failed"
    m = re.search(
        r'(\d+) scenarios\s+'
        r'(\d+) passed,?\s*'
        r'(?:(\d+) undefined,?\s*)?'
        r'(\d+) failed',
        stdout_text,
    )
    if m:
        scenarios = {
            'total': int(m.group(1)),
            'passed': int(m.group(2)),
            'undefined': int(m.group(3) or 0),
            'failed': int(m.group(4)),
        }
    else:
        # "N scenarios X passed" (all passed, no failures)
        m = re.search(r'(\d+) scenarios\s+(\d+) passed', stdout_text)
        if m:
            scenarios = {
                'total': int(m.group(1)),
                'passed': int(m.group(2)),
                'undefined': 0,
                'failed': 0,
            }

    # Steps: "N steps X passed, Y skipped, Z undefined, W failed"
    m = re.search(
        r'(\d+) steps\s+'
        r'(\d+) passed,?\s*'
        r'(?:(\d+) skipped,?\s*)?'
        r'(?:(\d+) undefined,?\s*)?'
        r'(\d+) failed',
        stdout_text,
    )
    if m:
        steps = {
            'total': int(m.group(1)),
            'passed': int(m.group(2)),
            'skipped': int(m.group(3) or 0),
            'undefined': int(m.group(4) or 0),
            'failed': int(m.group(5)),
        }

    if not scenarios:
        return None
    return {'scenarios': scenarios, 'steps': steps}


def parse_report_metrics(text: str) -> dict | None:
    """Extract scenario and step metrics from a markdown TCK report."""
    metrics = {}
    section = None
    for line in text.splitlines():
        heading = re.match(r'####\s+(.+)', line)
        if heading:
            section = heading.group(1).strip().lower()
            continue
        m = re.match(r'\|\s*\*?\*?(.+?)\*?\*?\s*\|\s*\*?\*?(\d+)\*?\*?\s*\|', line)
        if m:
            key = m.group(1).strip()
            val = int(m.group(2))
            prefix = ''
            if section == 'scenarios':
                prefix = 'Scenarios '
            elif section == 'steps':
                prefix = 'Steps '
            metrics[f'{prefix}{key}'] = val
    if not metrics:
        return None
    return metrics


def generate_diff_section(baseline: dict, current: dict) -> str:
    """Generate a markdown table comparing current report against baseline."""
    rows = []
    comparisons = [
        ('Scenarios Passed', True),
        ('Scenarios Failed', False),
        ('Scenarios Undefined', False),
        ('Steps Passed', True),
        ('Steps Failed', False),
        ('Steps Skipped', False),
    ]
    for label, higher_is_better in comparisons:
        b = baseline.get(label)
        c = current.get(label)
        if b is None or c is None:
            continue
        delta = c - b
        sign = '+' if delta > 0 else ''
        emoji = ''
        if delta > 0:
            emoji = ' :white_check_mark:' if higher_is_better else ' :x:'
        elif delta < 0:
            emoji = ' :x:' if higher_is_better else ' :white_check_mark:'
        rows.append(f'| {label} | {b} | {c} | {sign}{delta}{emoji} |')

    # Pass rate
    b_total = baseline.get('Scenarios Total', 0)
    c_total = current.get('Scenarios Total', 0)
    b_passed = baseline.get('Scenarios Passed', 0)
    c_passed = current.get('Scenarios Passed', 0)
    if b_total > 0 and c_total > 0:
        b_rate = b_passed / b_total * 100
        c_rate = c_passed / c_total * 100
        delta = c_rate - b_rate
        sign = '+' if delta > 0 else ''
        emoji = ' :white_check_mark:' if delta > 0 else (' :x:' if delta < 0 else '')
        rows.append(f'| Pass Rate | {b_rate:.1f}% | {c_rate:.1f}% | {sign}{delta:.1f}%{emoji} |')

    if not rows:
        return ''

    warning = ''
    if b_total > 0 and c_total > 0 and c_rate < b_rate:
        warning = (f'> [!WARNING]\n'
                   f'> **TCK pass rate regressed**: '
                   f'{c_rate:.1f}% < {b_rate:.1f}% (main)\n\n')

    lines = ['### TCK Report vs main', '']
    if warning:
        lines.append(warning)
    lines.extend(['| Metric | main | This PR | Delta |',
                   '|--------|------|---------|-------|'])
    lines.extend(rows)
    lines.append('')
    return '\n'.join(lines)


def generate_report(summary, skip_reasons: Counter, elapsed_s: float) -> str:
    """Generate a markdown report string."""
    lines = []
    lines.append('### TCK Test Report')
    lines.append('')
    lines.append('#### Scenarios')
    lines.append('')
    lines.append('| Metric | Value |')
    lines.append('|--------|-------|')
    lines.append(f'| Total | {summary["scenarios"]["total"]} |')
    lines.append(f'| **Passed** | **{summary["scenarios"]["passed"]}** |')
    lines.append(f'| Undefined | {summary["scenarios"]["undefined"]} |')
    lines.append(f'| Failed | {summary["scenarios"]["failed"]} |')

    skipped_total = sum(skip_reasons.values())
    failed_no_skip = summary['scenarios']['failed'] - skipped_total
    lines.append(f'| AST skipped (unsupported syntax) | {skipped_total} |')
    lines.append(f'| Executed failures (query error / result mismatch) | {failed_no_skip} |')
    lines.append('')

    if summary.get('steps'):
        s = summary['steps']
        lines.append('#### Steps')
        lines.append('')
        lines.append('| Metric | Value |')
        lines.append('|--------|-------|')
        lines.append(f'| Total | {s["total"]} |')
        lines.append(f'| Passed | {s["passed"]} |')
        lines.append(f'| Skipped | {s["skipped"]} |')
        lines.append(f'| Undefined | {s["undefined"]} |')
        lines.append(f'| Failed | {s["failed"]} |')
        lines.append('')

    lines.append(f'Elapsed: {elapsed_s:.1f}s')
    lines.append('')

    if skip_reasons:
        lines.append('#### Skipped Scenarios by Reason')
        lines.append('')
        lines.append('| Reason | Count |')
        lines.append('|--------|-------|')
        for reason, count in skip_reasons.most_common():
            lines.append(f'| {reason} | {count} |')
        lines.append('')

    return '\n'.join(lines)


def _status_counts(data: dict) -> dict:
    """Count steps by status."""
    counts = {}
    for info in data.values():
        s = info['status']
        counts[s] = counts.get(s, 0) + 1
    return counts


def _status_summary(title: str, data: dict) -> str:
    """One-line status summary for a step results dict."""
    counts = _status_counts(data)
    parts = ', '.join(f'{k}={v}' for k, v in sorted(counts.items()))
    return f'{title}: {len(data)} entries ({parts})'


def parse_step_results(json_path: str) -> dict | None:
    """Parse step-level results JSON into {(scenario, step_index): {'step': step_text, 'status': status}}."""
    if not json_path or not os.path.isfile(json_path):
        print(f"[run_tck] Step results file not found: {json_path}", file=sys.stderr, flush=True)
        return None
    try:
        with open(json_path) as f:
            data = json.load(f)
        result = {}
        for entry in data:
            key = (entry.get('scenario', ''), entry.get('index', -1))
            result[key] = {
                'step': entry.get('step', ''),
                'status': entry.get('status', 'UNKNOWN'),
            }
        print(f"[run_tck] Step results: {_status_summary(json_path, result)}", flush=True)
        return result
    except (json.JSONDecodeError, OSError) as e:
        print(f"[run_tck] Could not parse step results: {e}", file=sys.stderr, flush=True)
        return None


def generate_step_diff(baseline: dict, current: dict) -> str:
    """Compare step-level results and generate regression markdown.

    Always produces a section with diagnostic stats. When regressions are
    found, lists them with scenario + step detail.
    """
    if not baseline or not current:
        return ''

    regressions = []
    improvements = []

    baseline_keys = set(baseline.keys())
    current_keys = set(current.keys())
    common_keys = baseline_keys & current_keys
    only_baseline = baseline_keys - current_keys
    only_current = current_keys - baseline_keys

    for key in common_keys:
        b_info = baseline[key]
        c_info = current[key]
        scenario_name, step_idx = key
        step_text = b_info['step']
        b_status = b_info['status']
        c_status = c_info['status']

        if b_status == 'PASSED' and c_status != 'PASSED':
            regressions.append((scenario_name, step_idx, step_text, b_status, c_status))
        elif b_status != 'PASSED' and c_status == 'PASSED':
            improvements.append((scenario_name, step_idx, step_text, b_status, c_status))

    # Always build a section with stats
    lines = ['### Step-Level Regression', '']

    lines.append('#### Diagnostics')
    lines.append(f'- {_status_summary("Baseline", baseline)}')
    lines.append(f'- {_status_summary("Current", current)}')
    lines.append(f'- Common keys: {len(common_keys)}, '
                 f'only in baseline: {len(only_baseline)}, '
                 f'only in current: {len(only_current)}')
    lines.append('')

    # Count PASSED entries that disappeared from current as regressions
    missing_regressions = []
    for key in sorted(only_baseline):
        info = baseline[key]
        if info['status'] == 'PASSED':
            missing_regressions.append((key[0], key[1], info['step'], 'PASSED', 'MISSING'))

    if only_baseline:
        lines.append('<details>')
        lines.append('<summary>Keys only in baseline (first 10)</summary>')
        lines.append('')
        for key in sorted(only_baseline)[:10]:
            info = baseline[key]
            lines.append(f'- `[{info["status"]}] [{key[1]}] {info["step"]}` — *{key[0][:100]}*')
        if len(only_baseline) > 10:
            lines.append(f'- ... and {len(only_baseline) - 10} more')
        lines.append('')
        lines.append('</details>')
        lines.append('')

    regressions.extend(missing_regressions)

    if only_current:
        lines.append('<details>')
        lines.append('<summary>Keys only in current (first 10)</summary>')
        lines.append('')
        for key in sorted(only_current)[:10]:
            info = current[key]
            lines.append(f'- `[{info["status"]}] [{key[1]}] {info["step"]}` — *{key[0][:100]}*')
        if len(only_current) > 10:
            lines.append(f'- ... and {len(only_current) - 10} more')
        lines.append('')
        lines.append('</details>')
        lines.append('')

    if regressions:
        lines.append('> [!CAUTION]')
        lines.append(f'> **{len(regressions)} steps regressed** '
                      '(PASSED in baseline → FAILED/SKIPPED now)')
        lines.append('')
        lines.append('| Scenario | Step | Baseline | Current |')
        lines.append('|----------|------|----------|---------|')
        max_show = 50
        for scenario, idx, step, b_status, c_status in regressions[:max_show]:
            short_scenario = scenario[:80] + '...' if len(scenario) > 80 else scenario
            lines.append(f'| {short_scenario} | [{idx}] {step} | {b_status} | {c_status} |')
        if len(regressions) > max_show:
            lines.append(f'| ... | ... and {len(regressions) - max_show} more | ... | ... |')
        lines.append('')
    else:
        lines.append('> [!NOTE]')
        lines.append('> **No regressions detected** in the overlapping keys.')
        lines.append('')

    if improvements:
        lines.append('> [!TIP]')
        lines.append(f'> **{len(improvements)} steps improved** '
                      '(FAILED/SKIPPED in baseline → PASSED now)')
        lines.append('')
        lines.append('| Scenario | Step | Baseline | Current |')
        lines.append('|----------|------|----------|---------|')
        max_show = 20
        for scenario, idx, step, b_status, c_status in improvements[:max_show]:
            short_scenario = scenario[:80] + '...' if len(scenario) > 80 else scenario
            lines.append(f'| {short_scenario} | [{idx}] {step} | {b_status} | {c_status} |')
        if len(improvements) > max_show:
            lines.append(f'| ... | ... and {len(improvements) - max_show} more | ... | ... |')
        lines.append('')

    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(
        description="Run TCK tests with a managed eugraph-server instance")
    parser.add_argument("--server-bin", required=True,
                        help="Path to eugraph-server binary")
    parser.add_argument("--tck-bin", required=True,
                        help="Path to tck_tests binary")
    parser.add_argument("--port", type=int, default=9999,
                        help="Server port (default: 9999)")
    parser.add_argument("--host", default="127.0.0.1",
                        help="Server bind address (default: 127.0.0.1)")
    parser.add_argument("--data-dir", default="/tmp/eugraph_tck_data",
                        help="WiredTiger data directory (default: /tmp/eugraph_tck_data)")
    parser.add_argument("--features", default="features",
                        help="Feature file or directory (default: features/)")
    parser.add_argument("--timeout", type=int, default=30,
                        help="Server startup timeout in seconds (default: 30)")
    parser.add_argument("--keep-data", action="store_true",
                        help="Keep data directory after test")
    parser.add_argument("--report", default=None,
                        help="Write markdown report to this file path")
    parser.add_argument("--baseline-report", default=None,
                        help="Path to baseline TCK report for comparison")
    parser.add_argument("--step-results-file", default=None,
                        help="Path to step-level results JSON (generated by tck_tests)")
    parser.add_argument("--step-baseline-file", default=None,
                        help="Path to step-level baseline JSON for comparison")
    parser.add_argument("extra", nargs=argparse.REMAINDER,
                        help="Extra arguments passed to tck_tests")

    args = parser.parse_args()

    # ---- Validate binaries ----
    if not os.path.isfile(args.server_bin):
        print(f"[run_tck] Server binary not found: {args.server_bin}", file=sys.stderr)
        sys.exit(1)
    if not os.path.isfile(args.tck_bin):
        print(f"[run_tck] tck_tests binary not found: {args.tck_bin}", file=sys.stderr)
        sys.exit(1)

    # ---- Prepare data directory ----
    if os.path.exists(args.data_dir):
        print(f"[run_tck] Cleaning data directory: {args.data_dir}")
        shutil.rmtree(args.data_dir)

    # ---- Start server ----
    server_cmd = [args.server_bin, "--port", str(args.port),
                  "--data-dir", args.data_dir]
    print(f"[run_tck] Starting server: {' '.join(server_cmd)}", flush=True)

    server_log = open("/tmp/eugraph_tck_server.log", "w")
    server_proc = subprocess.Popen(
        server_cmd,
        stdin=subprocess.DEVNULL,
        stdout=server_log,
        stderr=subprocess.STDOUT,
    )

    # ---- Wait for server ----
    print(f"[run_tck] Waiting for server on {args.host}:{args.port} "
          f"(timeout={args.timeout}s)...", flush=True)
    if not wait_for_server(args.host, args.port, args.timeout, server_proc.pid):
        print("[run_tck] Server failed to start", file=sys.stderr, flush=True)
        stop_server(server_proc.pid)
        sys.exit(1)

    print(f"[run_tck] Server ready", flush=True)

    # ---- Run tck_tests ----
    tck_cmd = [args.tck_bin, args.features] + args.extra
    env = os.environ.copy()
    env["EUGRAPH_HOST"] = args.host
    env["EUGRAPH_PORT"] = str(args.port)
    if args.step_results_file:
        env["TCK_STEP_RESULTS_PATH"] = args.step_results_file

    print(f"[run_tck] Running: {' '.join(tck_cmd)}", flush=True)
    sys.stdout.flush()
    tck_start = time.monotonic()
    tck_result = subprocess.run(tck_cmd, env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True)
    tck_elapsed = time.monotonic() - tck_start

    # Print captured output
    if tck_result.stdout:
        sys.stdout.write(tck_result.stdout)
    if tck_result.stderr:
        sys.stderr.write(tck_result.stderr)

    print(f"[run_tck] tck_tests finished in {tck_elapsed:.1f}s "
          f"(exit={tck_result.returncode})", flush=True)

    # ---- Generate report ----
    if args.report:
        summary = parse_cucumber_summary(strip_ansi(tck_result.stdout or ''))
        skip_reasons = parse_skip_reasons(tck_result.stderr or '')
        if summary:
            report = generate_report(summary, skip_reasons, tck_elapsed)

            # Append baseline diff if available
            baseline_path = args.baseline_report or os.environ.get('TCK_BASELINE_REPORT_PATH')
            if baseline_path and os.path.isfile(baseline_path):
                try:
                    with open(baseline_path) as f:
                        baseline_text = f.read()
                    baseline_metrics = parse_report_metrics(baseline_text)
                    current_metrics = parse_report_metrics(report)
                    if baseline_metrics and current_metrics:
                        diff_section = generate_diff_section(baseline_metrics, current_metrics)
                        if diff_section:
                            report = report + '\n' + diff_section
                            print("[run_tck] Baseline comparison appended", flush=True)
                except OSError as e:
                    print(f"[run_tck] Could not read baseline report: {e}",
                          file=sys.stderr, flush=True)

            # Append step-level diff if available
            step_results_path = args.step_results_file or os.environ.get('TCK_STEP_RESULTS_PATH')
            step_baseline_path = args.step_baseline_file or os.environ.get('TCK_STEP_BASELINE_PATH')
            print(f"[run_tck] Step results file arg/env: {step_results_path or '(not set)'}", flush=True)
            print(f"[run_tck] Step baseline file arg/env: {step_baseline_path or '(not set)'}", flush=True)
            if step_results_path and os.path.isfile(step_results_path):
                print(f"[run_tck] Step results file exists (size={os.path.getsize(step_results_path)})", flush=True)
                current_steps = parse_step_results(step_results_path)
                baseline_steps = None
                if step_baseline_path:
                    if os.path.isfile(step_baseline_path):
                        print(f"[run_tck] Step baseline file exists (size={os.path.getsize(step_baseline_path)})", flush=True)
                        baseline_steps = parse_step_results(step_baseline_path)
                    else:
                        print(f"[run_tck] Step baseline file NOT FOUND: {step_baseline_path}", flush=True)
                else:
                    print(f"[run_tck] No step baseline path provided", flush=True)
                if current_steps and baseline_steps:
                    step_diff = generate_step_diff(baseline_steps, current_steps)
                    if step_diff:
                        report = report + '\n' + step_diff
                        print(f"[run_tck] Step-level comparison appended", flush=True)
                elif current_steps:
                    print(f"[run_tck] Skipping step comparison (baseline has no entries or missing)", flush=True)
            else:
                if step_results_path:
                    print(f"[run_tck] Step results file NOT FOUND: {step_results_path}", flush=True)
                else:
                    print(f"[run_tck] No step results file path configured", flush=True)

            with open(args.report, 'w') as f:
                f.write(report + '\n')
            print(f"[run_tck] Report written to {args.report}", flush=True)
        else:
            print("[run_tck] WARNING: Could not parse cucumber summary, "
                  "report not generated", file=sys.stderr, flush=True)

    # ---- Stop server ----
    server_status = stop_server(server_proc.pid)

    server_log.close()

    # ---- Clean up ----
    if not args.keep_data and os.path.exists(args.data_dir):
        shutil.rmtree(args.data_dir)

    # ---- Report ----
    server_crashed = False
    if server_status < 0:
        print(f"[run_tck] WARNING: Server crashed (signal {abs(server_status)})",
              file=sys.stderr, flush=True)
        server_crashed = True
        server_log_path = "/tmp/eugraph_tck_server.log"
        try:
            with open(server_log_path) as f:
                lines = f.readlines()
                tail = lines[-100:] if len(lines) > 100 else lines
                print(f"[run_tck] Server log tail ({len(tail)} lines):",
                      file=sys.stderr, flush=True)
                for line in tail:
                    print(f"[server] {line.rstrip()}", file=sys.stderr, flush=True)
        except OSError as e:
            print(f"[run_tck] Could not read server log: {e}",
                  file=sys.stderr, flush=True)
    elif server_status != 0:
        print(f"[run_tck] WARNING: Server exited with non-zero code {server_status}",
              file=sys.stderr, flush=True)

    if server_crashed or server_status != 0:
        print("[run_tck] Failing due to server crash", file=sys.stderr, flush=True)
        sys.exit(1)

    print(f"[run_tck] tck_tests exit code: {tck_result.returncode} (ignored)", flush=True)
    sys.exit(0)


if __name__ == "__main__":
    main()
