"""HTTP/WebSocket integration tests for the embedded EuGraph web console.

The CTest wrapper (scripts/run-web-console-tests.sh) starts eugraph-server with
a temporary data directory and exports WEB_PORT / BOLT_PORT.
"""

from __future__ import annotations

import base64
import gzip
import http.client
import json
import os
import pathlib
import re
import socket
import subprocess
import time

import pytest


WEB_PORT = int(os.environ.get("WEB_PORT", "17474"))
BOLT_PORT = int(os.environ.get("BOLT_PORT", "17694"))
THRIFT_PORT = int(os.environ.get("THRIFT_PORT", "19094"))
HOST = "127.0.0.1"


def request(method: str, path: str, headers: dict[str, str] | None = None) -> tuple[int, dict[str, str], bytes]:
    connection = http.client.HTTPConnection(HOST, WEB_PORT, timeout=10)
    try:
        connection.request(method, path, headers=headers or {})
        response = connection.getresponse()
        body = response.read()
        return response.status, {key.lower(): value for key, value in response.getheaders()}, body
    finally:
        connection.close()


def asset_path() -> str:
    status, _, body = request("GET", "/")
    assert status == 200
    match = re.search(rb"/assets/[A-Za-z0-9._-]+\.js", body)
    assert match, body[:200]
    return match.group(0).decode()


def ws_upgrade(origin: str | None) -> str:
    key = base64.b64encode(os.urandom(16)).decode()
    request_lines = [
        f"GET / HTTP/1.1",
        f"Host: {HOST}:{BOLT_PORT}",
        "Upgrade: websocket",
        "Connection: Upgrade",
        f"Sec-WebSocket-Key: {key}",
        "Sec-WebSocket-Version: 13",
    ]
    if origin is not None:
        request_lines.append(f"Origin: {origin}")
    payload = ("\r\n".join(request_lines) + "\r\n\r\n").encode()

    with socket.create_connection((HOST, BOLT_PORT), timeout=10) as sock:
        sock.sendall(payload)
        data = sock.recv(4096)
    return data.split(b"\r\n", 1)[0].decode(errors="replace")


def test_index_served_with_security_headers() -> None:
    status, headers, body = request("GET", "/")
    assert status == 200
    assert headers["content-type"].startswith("text/html")
    assert headers["cache-control"] == "no-store"
    assert headers["x-content-type-options"] == "nosniff"
    assert headers["x-frame-options"] == "DENY"
    assert headers["referrer-policy"] == "no-referrer"
    assert headers["server"] == "EuGraph"
    assert f"ws://{HOST}:{BOLT_PORT}" in headers["content-security-policy"]
    assert b"EuGraph" in body


def test_hashed_asset_served_with_immutable_cache() -> None:
    path = asset_path()
    status, headers, body = request("GET", path)
    assert status == 200
    assert headers["content-type"].startswith("text/javascript")
    assert headers["cache-control"] == "public, max-age=31536000, immutable"
    assert headers["etag"]
    assert len(body) > 1000


def test_gzip_variant_matches_raw_asset() -> None:
    path = asset_path()
    raw_status, _, raw_body = request("GET", path)
    gzip_status, gzip_headers, gzip_body = request("GET", path, {"Accept-Encoding": "gzip"})
    assert raw_status == 200
    assert gzip_status == 200
    assert gzip_headers["content-encoding"] == "gzip"
    assert gzip_headers["etag"].endswith('-gzip"')
    assert gzip.decompress(gzip_body) == raw_body


def test_conditional_get_returns_304() -> None:
    path = asset_path()
    _, headers, _ = request("GET", path)
    etag = headers["etag"]
    status, response_headers, body = request("GET", path, {"If-None-Match": etag})
    assert status == 304
    assert body == b""
    assert response_headers["etag"] == etag


def test_head_has_headers_but_no_body() -> None:
    path = asset_path()
    status, headers, body = request("HEAD", path)
    assert status == 200
    assert body == b""
    assert int(headers["content-length"]) > 1000


def test_info_endpoint() -> None:
    status, headers, body = request("GET", "/api/info")
    assert status == 200
    assert headers["content-type"].startswith("application/json")
    info = json.loads(body)
    assert info["product"] == "EuGraph"
    assert info["bolt"]["port"] == BOLT_PORT
    assert info["database"] == "default"
    assert info["assets"]["hash"]


def test_healthz() -> None:
    status, _, body = request("GET", "/healthz")
    assert status == 200
    assert body == b"ok\n"


def test_unknown_paths_are_404() -> None:
    assert request("GET", "/nope")[0] == 404
    assert request("GET", "/api/missing")[0] == 404
    assert request("GET", "/assets/missing.js")[0] == 404


def test_non_read_method_is_405() -> None:
    status, headers, _ = request("POST", "/api/info")
    assert status == 405
    assert headers["allow"] == "GET, HEAD"


def test_keep_alive_reuses_connection() -> None:
    connection = http.client.HTTPConnection(HOST, WEB_PORT, timeout=10)
    try:
        for _ in range(2):
            connection.request("GET", "/healthz")
            response = connection.getresponse()
            assert response.status == 200
            assert response.read() == b"ok\n"
    finally:
        connection.close()


def test_path_traversal_is_not_served() -> None:
    for target in (
        "/../etc/passwd",
        "/%2e%2e/etc/passwd",
        "/assets/../../etc/passwd",
    ):
        with socket.create_connection((HOST, WEB_PORT), timeout=10) as sock:
            sock.sendall(
                f"GET {target} HTTP/1.1\r\nHost: {HOST}\r\nConnection: close\r\n\r\n".encode()
            )
            data = sock.recv(8192)
        status_line = data.split(b"\r\n", 1)[0].decode(errors="replace")
        assert " 404 " in status_line or " 400 " in status_line, (target, status_line)
        assert b"root:" not in data


def test_websocket_rejects_external_origin() -> None:
    assert "403" in ws_upgrade("http://evil.example.com")


def test_websocket_accepts_loopback_and_absent_origin() -> None:
    assert "101" in ws_upgrade("http://127.0.0.1:5173")
    assert "101" in ws_upgrade("http://localhost:17474")
    assert "101" in ws_upgrade(None)


def test_reset_cancels_streaming_query_and_keeps_connection_usable() -> None:
    neo4j = pytest.importorskip("neo4j")
    driver = neo4j.GraphDatabase.driver(f"bolt://{HOST}:{BOLT_PORT}", auth=("neo4j", "eugraph"))
    try:
        session = driver.session(database="default")
        result = session.run("UNWIND range(1, 2000000) AS x RETURN x")
        assert next(iter(result))["x"] == 1

        started = time.monotonic()
        session.close()
        assert time.monotonic() - started < 5.0

        session2 = driver.session(database="default")
        assert session2.run("RETURN 2 AS ok").single()["ok"] == 2
        session2.close()
    finally:
        driver.close()


def test_web_port_conflict_exits_cleanly() -> None:
    server = os.environ.get("EUGRAPH_SERVER_BINARY")
    if not server:
        pytest.skip("EUGRAPH_SERVER_BINARY is not set")

    # Keep the tiny WT data directory on disk (cwd is the build directory).
    data_dir = pathlib.Path.cwd() / f"web-port-conflict-{os.getpid()}"
    try:
        result = subprocess.run(
            [
                server,
                "--thrift-port",
                str(THRIFT_PORT + 1),
                "--bolt-port",
                str(BOLT_PORT + 1),
                "--web-port",
                str(WEB_PORT),
                "--data-dir",
                str(data_dir),
            ],
            capture_output=True,
            text=True,
            timeout=30,
        )
        assert result.returncode == 1, result.stdout + result.stderr
        assert "Address already in use" in result.stdout + result.stderr
    finally:
        import shutil

        shutil.rmtree(data_dir, ignore_errors=True)
