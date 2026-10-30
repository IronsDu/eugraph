#!/usr/bin/env bash
#
# Start eugraph-server, run the web-console HTTP/WebSocket integration tests,
# stop the server, and preserve the log on failure.
#
# Usage: scripts/run-web-console-tests.sh <server> <pytest> <test-script>
set -euo pipefail

SERVER="${1:?server binary required}"
PYTEST="${2:?pytest binary required}"
TEST_SCRIPT="${3:?test script required}"

WEB_PORT="${WEB_PORT:-17474}"
BOLT_PORT="${BOLT_PORT:-17694}"
THRIFT_PORT="${THRIFT_PORT:-19094}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Keep the WT data directory on disk: /tmp is often tmpfs, which makes WiredTiger
# fail with misleading "empty result / commit failed" errors.
WORK_DIR="${WORK_DIR:-${ROOT}/build/web-console-test-$$}"
LOG_FILE="${WORK_DIR}/web-server.log"

cleanup() {
    local rc=$?
    if [[ -n "${SERVER_PID:-}" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill -TERM -- "-$SERVER_PID" 2>/dev/null || kill "$SERVER_PID" 2>/dev/null || true
        sleep 1
        kill -KILL -- "-$SERVER_PID" 2>/dev/null || true
    fi
    if [[ "$rc" -eq 0 ]]; then
        rm -rf "$WORK_DIR"
    else
        echo "[web] test failed (rc=$rc); preserving ${WORK_DIR} (server log ${LOG_FILE})" >&2
        if [[ -f "$LOG_FILE" ]]; then
            echo "----- ${LOG_FILE} (tail -60) -----" >&2
            tail -60 "$LOG_FILE" >&2
        fi
    fi
    exit "$rc"
}
trap cleanup EXIT

mkdir -p "$WORK_DIR"

setsid "$SERVER" \
    --thrift-port "$THRIFT_PORT" \
    --bolt-port "$BOLT_PORT" \
    --web-port "$WEB_PORT" \
    --web-bind 127.0.0.1 \
    --data-dir "$WORK_DIR/data" \
    > "$LOG_FILE" 2>&1 &
SERVER_PID=$!

ready=0
for _ in $(seq 1 "${EUGRAPH_TEST_READY_TIMEOUT:-60}"); do
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "ERROR: eugraph-server died during startup" >&2
        cat "$LOG_FILE" >&2
        exit 1
    fi
    if python3 - "$WEB_PORT" <<'PY'
import sys
import time
import urllib.request

port = sys.argv[1]
try:
    with urllib.request.urlopen(f"http://127.0.0.1:{port}/healthz", timeout=1) as response:
        if response.status == 200 and response.read() == b"ok\n":
            raise SystemExit(0)
except Exception:
    pass
raise SystemExit(1)
PY
    then
        ready=1
        break
    fi
    sleep 1
done

if [[ "$ready" -ne 1 ]]; then
    echo "ERROR: web console did not become ready on 127.0.0.1:${WEB_PORT}" >&2
    cat "$LOG_FILE" >&2
    exit 1
fi

export WEB_PORT BOLT_PORT THRIFT_PORT
# Used by the port-conflict regression test.
export EUGRAPH_SERVER_BINARY="$SERVER"
"$PYTEST" -q "$TEST_SCRIPT"
