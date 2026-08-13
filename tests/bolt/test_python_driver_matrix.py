"""Run the Bolt integration suite with several official neo4j driver versions."""

import os
import subprocess
import sys
from pathlib import Path

import pytest


DRIVER_VERSIONS = [
    "4.4.12",
    "5.20.0",
]


def _project_root():
    return Path(__file__).resolve().parents[2]


def _cache_root():
    return Path(
        os.environ.get(
            "EUGRAPH_DRIVER_VENV_CACHE",
            _project_root() / "build" / "driver_venvs",
        )
    )


def _python_for_version(version):
    env_dir = _cache_root() / f"neo4j-{version}"
    python = env_dir / ("bin/python" if os.name != "nt" else "Scripts/python.exe")

    if python.exists():
        check = subprocess.run(
            [str(python), "-c", "import neo4j, pytest"],
            capture_output=True,
            text=True,
        )
        if check.returncode == 0:
            return python
        subprocess.run(
            [str(python), "-m", "pip", "install", "--quiet", f"neo4j=={version}", "pytest"],
            check=True,
        )
        return python

    env_dir.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, "-m", "venv", str(env_dir)], check=True)
    subprocess.run(
        [str(python), "-m", "pip", "install", "--quiet", f"neo4j=={version}", "pytest"],
        check=True,
    )
    return python


@pytest.mark.parametrize("driver_version", DRIVER_VERSIONS)
def test_official_driver_compatibility(driver_version):
    python = _python_for_version(driver_version)
    integration_test = _project_root() / "tests" / "bolt" / "test_python_driver_integration.py"
    env = os.environ.copy()
    env["EUGRAPH_BOLT_PORT"] = os.environ.get("EUGRAPH_BOLT_PORT", "7687")

    result = subprocess.run(
        [str(python), "-m", "pytest", str(integration_test), "-q"],
        capture_output=True,
        text=True,
        env=env,
    )

    assert result.returncode == 0, result.stderr or result.stdout
