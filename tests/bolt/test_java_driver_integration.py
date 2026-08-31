"""Run the Bolt path-return integration test with the official Java driver."""

import os
import subprocess
import sys
from pathlib import Path


def test_java_driver_path_return():
    project_root = Path(__file__).resolve().parents[2]
    java_source = Path(__file__).resolve().parent / "TestJavaDriver.java"
    lib_dir = os.environ.get("NEO4J_JAVA_DRIVER_LIB_DIR")
    assert lib_dir, "NEO4J_JAVA_DRIVER_LIB_DIR is not set"
    classpath = str(Path(lib_dir) / "*")

    workdir = Path(os.environ.get("JAVA_DRIVER_WORKDIR", "/tmp/java-driver-bolt-test"))
    workdir.mkdir(parents=True, exist_ok=True)
    class_out = workdir / "classes"
    class_out.mkdir(exist_ok=True)

    env = os.environ.copy()
    env["EUGRAPH_BOLT_PORT"] = os.environ.get("EUGRAPH_BOLT_PORT", "17687")

    javac = subprocess.run(
        ["javac", "-cp", classpath, "-d", str(class_out), str(java_source)],
        capture_output=True,
        text=True,
    )
    assert javac.returncode == 0, javac.stderr or javac.stdout

    java = subprocess.run(
        ["java", "-cp", f"{classpath}:{class_out}", "TestJavaDriver"],
        capture_output=True,
        text=True,
        env=env,
    )
    assert java.returncode == 0, java.stderr or java.stdout
    assert "Java driver path test passed" in java.stdout


if __name__ == "__main__":
    sys.exit(0 if __import__("pytest").main([__file__, "-v"]) == 0 else 1)
