#!/bin/bash
#
# Start eugraph-server, run JS WebSocket Bolt driver integration tests, stop server.
# Invoked by CTest's `bolt_js_ws_driver_integration_tests` (see CMakeLists.txt).
#
# Usage:
#   ./scripts/run-bolt-js-ws-tests.sh \
#       <server_binary> <pytest_binary> <test_script> <node_binary> <npm_binary>
#
# Optional env overrides:
#   BOLT_PORT     (default 17687)
#   THRIFT_PORT   (default 19090)
#   WORK_DIR      (default: mktemp -d)
#
set -euo pipefail

SERVER="${1:?server binary required}"
PYTEST="${2:?pytest binary required}"
TEST_SCRIPT="${3:?pytest driver matrix required}"
NODE="${4:?node binary required}"
NPM="${5:?npm binary required}"

BOLT_PORT="${BOLT_PORT:-17687}"
THRIFT_PORT="${THRIFT_PORT:-19090}"
WORK_DIR="${WORK_DIR:-$(mktemp -d -t bolt-node-test-XXXX)}"
LOG_FILE="${WORK_DIR}/bolt-server.log"

cleanup() {
    if [[ -n "${SERVER_PID:-}" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf "$WORK_DIR"
}
trap cleanup EXIT

mkdir -p "$WORK_DIR"

"$SERVER" --port "$THRIFT_PORT" --bolt-port "$BOLT_PORT" --data-dir "$WORK_DIR/data" \
    > "$LOG_FILE" 2>&1 &
SERVER_PID=$!

# Poll for server startup (max 30s)
for i in $(seq 1 30); do
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "ERROR: eugraph-server died during startup" >&2
        cat "$LOG_FILE" >&2
        exit 1
    fi
    if echo '' | timeout 1 bash -c "exec 3<>/dev/tcp/localhost/${BOLT_PORT}" 2>/dev/null; then
        echo "Server started (PID $SERVER_PID, took ${i}s)"
        break
    fi
    sleep 1
done

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "ERROR: eugraph-server died during startup" >&2
    cat "$LOG_FILE" >&2
    exit 1
fi

export EUGRAPH_BOLT_PORT="$BOLT_PORT"
export NODE_EXECUTABLE="$NODE"
export NPM_EXECUTABLE="$NPM"

"$PYTEST" "$TEST_SCRIPT" -v
