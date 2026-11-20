#!/usr/bin/env python3
"""崩溃/恢复 chaos（手动 L3，不进 CI）：构建中 SIGKILL → 重启 → 索引必须 ERROR 且可清理。

用 eugraph-shell 走 Thrift，不依赖 Python 驱动。

用法：
  python3 tests/index_build_chaos_test.py \
    --server build/release/eugraph-server --shell build/release/eugraph-shell \
    --rows 100000 --port 19997

判据：
  1. CREATE INDEX 返回 "building"；
  2. 在索引仍为 BUILDING 时 SIGKILL server；
  3. 用同一个 data-dir 重启；
  4. SHOW INDEXES 必须显示该索引 ERROR（绝不 PUBLIC）；
  5. 清理后再次启动/清理 data-dir。
"""

import argparse
import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def wait_port(host: str, port: int, timeout: float = 30.0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.settimeout(0.5)
            if sock.connect_ex((host, port)) == 0:
                return
        time.sleep(0.1)
    raise RuntimeError(f"server did not listen on {host}:{port}")


def run_shell(shell: str, port: int, script: str, timeout: float = 180.0) -> str:
    result = subprocess.run(
        [shell, "-p", str(port)],
        input=script,
        text=True,
        capture_output=True,
        timeout=timeout,
    )
    output = result.stdout + result.stderr
    if result.returncode != 0:
        raise RuntimeError(f"shell failed ({result.returncode}): {output[-2000:]}")
    return output


def start_server(server: str, data_dir: Path, port: int) -> subprocess.Popen:
    proc = subprocess.Popen(
        [server, "--thrift-port", str(port), "--bolt-port", "0", "--data-dir", str(data_dir),
         "--wt-txn-sync", "none"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        stdin=subprocess.DEVNULL,
    )
    wait_port("127.0.0.1", port)
    return proc


def stop_server(proc: subprocess.Popen, sig: int = signal.SIGTERM) -> None:
    if proc.poll() is not None:
        return
    try:
        os.kill(proc.pid, sig)
    except ProcessLookupError:
        return
    try:
        proc.wait(timeout=15)
    except subprocess.TimeoutExpired:
        os.kill(proc.pid, signal.SIGKILL)
        proc.wait(timeout=10)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", default="build/release/eugraph-server")
    parser.add_argument("--shell", default="build/release/eugraph-shell")
    parser.add_argument("--rows", type=int, default=100000)
    parser.add_argument("--port", type=int, default=19997)
    parser.add_argument("--data-dir", default="")
    args = parser.parse_args()

    data_dir = Path(args.data_dir) if args.data_dir else Path(tempfile.mkdtemp(prefix="eugraph_index_chaos_"))
    data_dir.mkdir(parents=True, exist_ok=True)
    server = None
    try:
        server = start_server(args.server, data_dir, args.port)
        print(f"[chaos] loading {args.rows} vertices...", flush=True)
        run_shell(
            args.shell, args.port,
            f"UNWIND range(1,{args.rows}) AS i CREATE (:Person {{id:i, email:'e'+toString(i)}});\n",
            timeout=300,
        )
        print("[chaos] creating index (async)...", flush=True)
        created = run_shell(
            args.shell, args.port,
            "CREATE INDEX idx_person_email FOR (n:Person) ON (n.email);\n",
            timeout=60,
        )
        if "building" not in created:
            print(f"[chaos] expected async building result, got:\n{created}", file=sys.stderr)
            return 2

        # 确认仍是 BUILDING 再 kill；如果构建太快已经 PUBLIC，说明数据量不够。
        state = run_shell(args.shell, args.port, "SHOW INDEXES;\n", timeout=30)
        if "BUILDING" not in state:
            print(f"[chaos] build finished before kill; increase --rows. state:\n{state}", file=sys.stderr)
            return 3

        print("[chaos] SIGKILL during BUILDING", flush=True)
        stop_server(server, signal.SIGKILL)
        server = None

        print("[chaos] restarting with same data dir...", flush=True)
        server = start_server(args.server, data_dir, args.port)
        recovered = run_shell(args.shell, args.port, "SHOW INDEXES;\n", timeout=60)
        if "idx_person_email" not in recovered or "ERROR" not in recovered:
            print(f"[chaos] FAILED: expected ERROR after restart, got:\n{recovered}", file=sys.stderr)
            return 4
        if "PUBLIC" in recovered:
            print(f"[chaos] FAILED: index became PUBLIC after crash:\n{recovered}", file=sys.stderr)
            return 5
        print("[chaos] PASS: index recovered as ERROR", flush=True)
        return 0
    finally:
        if server is not None:
            stop_server(server)
        if not args.data_dir:
            shutil.rmtree(data_dir, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
