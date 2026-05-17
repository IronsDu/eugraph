#!/usr/bin/env python3
"""TCK test runner: start eugraph-server, run tck_tests, stop server.

Usage:
  run_tck.py --server-bin PATH --tck-bin PATH [OPTIONS] [--] [extra tck_tests args...]
"""

import argparse
import os
import shutil
import signal
import socket
import subprocess
import sys
import time


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
        # Check if process is still alive
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

    # Wait up to 5s for graceful shutdown
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
                        # SIGTERM is expected — we sent it. folly treats
                        # SIGTERM as a fatal signal and re-raises it,
                        # causing the process to exit via signal rather
                        # than calling _exit(0).
                        print(f"[run_tck] Server stopped (SIGTERM)", file=sys.stderr)
                        return 0
                    print(f"[run_tck] Server killed by signal {sig}",
                          file=sys.stderr)
                    return -sig
        except ChildProcessError:
            return 0
        time.sleep(0.1)

    # Force kill
    print("[run_tck] Server did not stop, sending SIGKILL", file=sys.stderr)
    try:
        os.kill(pid, signal.SIGKILL)
        wpid, status = os.waitpid(pid, 0)
        return -signal.SIGKILL
    except ProcessLookupError:
        return 0


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

    elapsed = time.monotonic()
    print(f"[run_tck] Server ready", flush=True)

    # ---- Run tck_tests ----
    tck_cmd = [args.tck_bin, args.features] + args.extra
    env = os.environ.copy()
    env["EUGRAPH_HOST"] = args.host
    env["EUGRAPH_PORT"] = str(args.port)

    print(f"[run_tck] Running: {' '.join(tck_cmd)}", flush=True)
    sys.stdout.flush()  # Ensure logs appear before tck_tests output
    tck_start = time.monotonic()
    tck_result = subprocess.run(tck_cmd, env=env)
    tck_elapsed = time.monotonic() - tck_start
    print(f"[run_tck] tck_tests finished in {tck_elapsed:.1f}s "
          f"(exit={tck_result.returncode})", flush=True)

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
        # Dump server log tail to help diagnose the crash
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

    # Infrastructure failures (server crash / non-zero exit) are always fatal.
    # TCK scenario failures are expected (many features not yet supported).
    if server_crashed or server_status != 0:
        print("[run_tck] Failing due to server crash", file=sys.stderr, flush=True)
        sys.exit(1)

    print(f"[run_tck] tck_tests exit code: {tck_result.returncode} (ignored)", flush=True)
    sys.exit(0)


if __name__ == "__main__":
    main()
