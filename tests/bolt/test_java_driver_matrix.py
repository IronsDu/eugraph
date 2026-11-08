"""Run the Bolt integration suite with several official Neo4j Java drivers.

The Java driver is not installed through a package manager in CI; the 5.28
driver comes from the Neo4j distribution (NEO4J_JAVA_DRIVER_LIB_DIR), while the
Bolt 4.4 driver is downloaded from Maven Central into the build cache.  The
matrix is intentionally explicit so the tested protocol/driver generations are
visible in one place, matching the Python and JS matrices.
"""

import os
import subprocess
import sys
import urllib.request
from pathlib import Path

import pytest


# Keep this list aligned with test_python_driver_matrix.py / test_js_driver_matrix.py:
# one Bolt 4.4 driver and one current 5.x driver.
JAVA_DRIVER_VERSIONS = [
    "4.4.3",
    "5.28.15",
]


def _project_root():
    return Path(__file__).resolve().parents[2]


def _cache_root():
    return Path(
        os.environ.get(
            "EUGRAPH_JAVA_DRIVER_CACHE",
            _project_root() / "build" / "java_driver_jars",
        )
    )


def _driver_jar_for_version(version):
    """Return the driver jar, downloading the pinned 4.4 artifact if needed."""
    override = os.environ.get(f"EUGRAPH_JAVA_DRIVER_{version.replace('.', '_')}_JAR")
    if override:
        jar = Path(override)
        assert jar.is_file(), f"driver jar not found: {jar}"
        return jar

    if version.startswith("4.4."):
        jar = _cache_root() / f"neo4j-java-driver-{version}.jar"
        if not jar.is_file():
            jar.parent.mkdir(parents=True, exist_ok=True)
            url = (
                "https://repo1.maven.org/maven2/org/neo4j/driver/"
                f"neo4j-java-driver/{version}/neo4j-java-driver-{version}.jar"
            )
            try:
                urllib.request.urlretrieve(url, jar)
            except Exception as exc:  # noqa: BLE001 - fail with a useful message
                pytest.fail(f"cannot download Neo4j Java driver {version}: {exc}")
        return jar

    lib_dir = os.environ.get("NEO4J_JAVA_DRIVER_LIB_DIR")
    assert lib_dir, "NEO4J_JAVA_DRIVER_LIB_DIR is not set"
    jar = Path(lib_dir) / f"neo4j-java-driver-{version}.jar"
    assert jar.is_file(), f"driver jar not found: {jar}"
    return jar


def _classpath(version):
    lib_dir = os.environ.get("NEO4J_JAVA_DRIVER_LIB_DIR")
    assert lib_dir, "NEO4J_JAVA_DRIVER_LIB_DIR is not set"
    # The 5.28 driver from the Neo4j distribution is unshaded; its dependencies
    # are in the same lib directory.  The 4.4 driver is shaded, but putting it
    # first still lets the JVM resolve any shared helpers from the lib dir.
    return f"{_driver_jar_for_version(version)}{os.pathsep}{Path(lib_dir) / '*'}"


@pytest.mark.parametrize("driver_version", JAVA_DRIVER_VERSIONS)
def test_official_java_driver_compatibility(driver_version):
    project_root = _project_root()
    java_source = project_root / "tests" / "bolt" / "TestJavaDriver.java"
    workdir = Path(
        os.environ.get("JAVA_DRIVER_WORKDIR", "/tmp/java-driver-bolt-test")
    )
    classes = workdir / f"classes-{driver_version}"
    classes.mkdir(parents=True, exist_ok=True)
    cp = _classpath(driver_version)

    env = os.environ.copy()
    env["EUGRAPH_BOLT_PORT"] = os.environ.get("EUGRAPH_BOLT_PORT", "17687")

    javac = subprocess.run(
        ["javac", "-cp", cp, "-d", str(classes), str(java_source)],
        capture_output=True,
        text=True,
    )
    assert javac.returncode == 0, javac.stderr or javac.stdout

    java = subprocess.run(
        ["java", "-cp", f"{cp}{os.pathsep}{classes}", "TestJavaDriver"],
        capture_output=True,
        text=True,
        env=env,
        timeout=180,
    )
    assert java.returncode == 0, java.stderr or java.stdout
    assert "Java driver integration test passed" in java.stdout
