"""Run the Bolt WebSocket integration suite with several official JS drivers.

The official ``neo4j-driver`` package in Node.js normally uses raw TCP for the
``lib/`` entrypoint.  To exercise EuGraph's WebSocket transport we explicitly
load the driver's browser bundle in Node and provide the ``ws`` WebSocket
implementation.

Each driver version is installed into an isolated npm prefix under
``build/driver_node_modules`` (or ``EUGRAPH_DRIVER_NODE_CACHE``), mirroring the
Python driver matrix in ``test_python_driver_matrix.py``.
"""

import os
import shutil
import subprocess
from pathlib import Path

import pytest


NODE_DRIVER_VERSIONS = [
    "4.4.11",
    "5.20.0",
    "6.2.0",
]


def _project_root():
    return Path(__file__).resolve().parents[2]


def _cache_root():
    return Path(
        os.environ.get(
            "EUGRAPH_DRIVER_NODE_CACHE",
            _project_root() / "build" / "driver_node_modules",
        )
    )


def _node_binary():
    node = os.environ.get("NODE_EXECUTABLE") or shutil.which("node")
    if not node:
        pytest.skip("node not found")
    return node


def _npm_binary():
    npm = os.environ.get("NPM_EXECUTABLE") or shutil.which("npm")
    if not npm:
        pytest.skip("npm not found")
    return npm


def _driver_root_for_version(version):
    prefix = _cache_root() / f"neo4j-{version}"
    driver_package = prefix / "node_modules" / "neo4j-driver" / "package.json"
    ws_package = prefix / "node_modules" / "ws" / "package.json"

    if not driver_package.exists() or not ws_package.exists():
        prefix.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            [
                _npm_binary(),
                "install",
                "--prefix",
                str(prefix),
                "--no-audit",
                "--no-fund",
                f"neo4j-driver@{version}",
                "ws@8",
            ],
            check=True,
        )

    return prefix


@pytest.mark.parametrize("driver_version", NODE_DRIVER_VERSIONS)
def test_official_node_driver_ws_compatibility(driver_version):
    driver_root = _driver_root_for_version(driver_version)
    node = _node_binary()
    test_file = _project_root() / "tests" / "bolt" / "test_js_ws_driver.cjs"

    env = os.environ.copy()
    env["EUGRAPH_BOLT_PORT"] = os.environ.get("EUGRAPH_BOLT_PORT", "17687")
    env["EUGRAPH_NODE_DRIVER_PATH"] = str(driver_root)

    result = subprocess.run(
        [node, "--test", str(test_file)],
        capture_output=True,
        text=True,
        env=env,
        timeout=180,
    )

    assert result.returncode == 0, result.stderr or result.stdout
