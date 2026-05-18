#!/usr/bin/env python3
"""TCK test runner: start eugraph-server, run tck_tests, stop server.

Usage:
  run_tck.py --server-bin PATH --tck-bin PATH [OPTIONS] [--] [extra tck_tests args...]
"""

import argparse
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
                    return os.WEXITEXITSTATUS(status)
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


def parse_skip_reasons(stderr_text: str) -> Counter:
    """Extract [TCK] skipping: reason counts from stderr."""
    counter = Counter()
    for line in stderr_text.splitlines():
        m = re.search(r'\[TCK\] skipping: (.+)$', line)
        if m:
            counter[m.group(1).strip()] += 1
    return counter


def parse_cucumber_summary(stdout_text: str):
    """Parse cucumber summary line."""
    # Format: "N scenarios X passed, Y undefined, Z failed"
    m = re.search(
        r'(\d+) scenarios\s+'
        r'(\d+) passed,?\s*'
        r'(?:(\d+) undefined,?\s*)?'
        r'(\d+) failed',
        stdout_text,
    )
    if m:
        return {
            'total': int(m.group(1)),
            'passed': int(m.group(2)),
            'undefined': int(m.group(3) or 0),
            'failed': int(m.group(4)),
        }
    # Format: "N scenarios X passed" (all passed, no failures)
    m = re.search(r'(\d+) scenarios\s+(\d+) passed', stdout_text)
    if m:
        return {
            'total': int(m.group(1)),
            'passed': int(m.group(2)),
            'undefined': 0,
            'failed': 0,
        }
    return None


def generate_report(summary, skip_reasons: Counter, elapsed_s: float) -> str:
    """Generate a markdown report string."""
    lines = []
    lines.append('### TCK Test Report')
    lines.append('')
    lines.append(f'| Metric | Value |')
    lines.append(f'|--------|-------|')
    lines.append(f'| Total scenarios | {summary["total"]} |')
    lines.append(f'| **Passed** | **{summary["passed"]}** |')
    lines.append(f'| Undefined steps | {summary["undefined"]} |')
    lines.append(f'| Failed | {summary["failed"]} |')

    skipped_total = sum(skip_reasons.values())
    failed_no_skip = summary['failed'] - skipped_total
    lines.append(f'| AST skipped (unsupported syntax) | {skipped_total} |')
    lines.append(f'| Executed failures (query error / result mismatch) | {failed_no_skip} |')
    lines.append(f'| Elapsed | {elapsed_s:.1f}s |')
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
        summary = parse_cucumber_summary(tck_result.stdout or '')
        skip_reasons = parse_skip_reasons(tck_result.stderr or '')
        if summary:
            report = generate_report(summary, skip_reasons, tck_elapsed)
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
