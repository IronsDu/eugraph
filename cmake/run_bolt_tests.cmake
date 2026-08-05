# run_bolt_tests.cmake — start eugraph-server, run Bolt Python tests, stop server
# Requires: SERVER, PYTEST, TEST_SCRIPT (passed via -D)

set(BOLT_PORT 17687)
set(THRIFT_PORT 19090)
set(DATA_DIR "${CMAKE_CURRENT_BINARY_DIR}/bolt-test-data")

file(REMOVE_RECURSE "${DATA_DIR}")

set(SHELL_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/bolt_test_runner.sh")
file(WRITE "${SHELL_SCRIPT}"
"#!/bin/bash
set -euo pipefail
DATA_DIR='${DATA_DIR}'
rm -rf \"\$DATA_DIR\"
'${SERVER}' --port ${THRIFT_PORT} --bolt-port ${BOLT_PORT} --data-dir \"\$DATA_DIR\" \\
    > '${CMAKE_CURRENT_BINARY_DIR}/bolt-server.log' 2>&1 &
SERVER_PID=\$!
trap 'kill \$SERVER_PID 2>/dev/null; rm -rf \"\$DATA_DIR\"' EXIT

# Poll until server is accepting connections (up to 30 seconds)
for i in \$(seq 1 30); do
    if ! kill -0 \$SERVER_PID 2>/dev/null; then
        echo 'ERROR: eugraph-server died during startup' >&2
        cat '${CMAKE_CURRENT_BINARY_DIR}/bolt-server.log' >&2
        exit 1
    fi
    if echo '' | timeout 1 bash -c \"exec 3<>/dev/tcp/localhost/${BOLT_PORT}\" 2>/dev/null; then
        echo \"Server started (PID \$SERVER_PID, took \${i}s)\"
        break
    fi
    sleep 1
done

if ! kill -0 \$SERVER_PID 2>/dev/null; then
    echo 'ERROR: eugraph-server died during startup' >&2
    cat '${CMAKE_CURRENT_BINARY_DIR}/bolt-server.log' >&2
    exit 1
fi

export EUGRAPH_BOLT_PORT=${BOLT_PORT}
'${PYTEST}' '${TEST_SCRIPT}' -v
"
)

execute_process(
    COMMAND bash "${SHELL_SCRIPT}"
    RESULT_VARIABLE test_result
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)

if(NOT test_result EQUAL 0)
    message(FATAL_ERROR "Bolt integration tests failed (exit code ${test_result})")
endif()

message(STATUS "Bolt integration tests passed")
