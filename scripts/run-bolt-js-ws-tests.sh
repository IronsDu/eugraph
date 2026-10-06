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
PROBE="$(dirname "$(readlink -f "$0")")/bolt_ready_probe.py"

cleanup() {
    local rc=$?
    if [[ -n "${SERVER_PID:-}" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
    fi
    # **失败时保留 WORK_DIR**（内含 bolt-server.log）便于诊断；成功才清理。
    # 并且**必须 `exit $rc`**：否则 trap 里最后一条成功命令会把脚本退出码覆盖成 0 ⇒ 包装脚本谎报成功。
    if [[ "$rc" -eq 0 ]]; then
        rm -rf "$WORK_DIR"
    else
        echo "[wrapper] 测试失败（rc=$rc）⇒ 保留诊断目录: ${WORK_DIR}（服务端日志 ${LOG_FILE}）" >&2
    fi
    exit "$rc"
}
trap cleanup EXIT

mkdir -p "$WORK_DIR"

"$SERVER" --thrift-port "$THRIFT_PORT" --bolt-port "$BOLT_PORT" --data-dir "$WORK_DIR/data" \
    > "$LOG_FILE" 2>&1 &
SERVER_PID=$!

# Poll for server startup (max 180s；ASan 构建启动更慢，30s 会误判为"服务端未就绪")
for i in $(seq 1 180); do
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "ERROR: eugraph-server died during startup" >&2
        cat "$LOG_FILE" >&2
        exit 1
    fi
    # 就绪判据：Bolt **握手必须被应答**。仅 TCP connect 成功不够 —— 端口已在监听但
    # Bolt handler 尚未注册完时，客户端连接会被拒（驱动侧 ServiceUnavailableException）。
    if python3 "$PROBE" "$BOLT_PORT" 2>/dev/null; then
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
