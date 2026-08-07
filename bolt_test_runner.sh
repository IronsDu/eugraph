#!/bin/bash
set -euo pipefail
DATA_DIR='/mnt/f/code/eugraph/bolt-test-data'
rm -rf "$DATA_DIR"
'/mnt/f/code/eugraph/build/release/eugraph-server' --port 19090 --bolt-port 17687 --data-dir "$DATA_DIR" \
    > '/mnt/f/code/eugraph/bolt-server.log' 2>&1 &
SERVER_PID=$!
trap 'kill $SERVER_PID 2>/dev/null; rm -rf "$DATA_DIR"' EXIT

# Poll until server is accepting connections (up to 30 seconds)
for i in $(seq 1 30); do
    if ! kill -0 $SERVER_PID 2>/dev/null; then
        echo 'ERROR: eugraph-server died during startup' >&2
        cat '/mnt/f/code/eugraph/bolt-server.log' >&2
        exit 1
    fi
    if echo '' | timeout 1 bash -c "exec 3<>/dev/tcp/localhost/17687" 2>/dev/null; then
        echo "Server started (PID $SERVER_PID, took ${i}s)"
        break
    fi
    sleep 1
done

if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo 'ERROR: eugraph-server died during startup' >&2
    cat '/mnt/f/code/eugraph/bolt-server.log' >&2
    exit 1
fi

export EUGRAPH_BOLT_PORT=17687
'/home/dodo/.local/bin/pytest' '/mnt/f/code/eugraph/tests/bolt/test_bolt_integration.py' -v
