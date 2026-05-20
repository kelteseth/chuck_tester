"""Test runner for the singleton example.

Handles start, test execution, and cleanup — like QTest's
initTestCase / cleanupTestCase.

Usage:
    uv run examples/singleton/test_suite.py
"""

import os
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


def find_exe() -> Path:
    env = os.environ.get("SINGLETON_EXE")
    if env:
        return Path(env)
    candidates = sorted(REPO.glob("build/*/examples/singleton/singleton_example.exe"))
    if not candidates:
        candidates = sorted(REPO.glob("build/*/examples/singleton/singleton_example"))
    if not candidates:
        sys.exit("singleton_example not found — build the project first")
    return candidates[-1]


def main():
    exe = find_exe()
    proc = None
    rc = 1

    try:
        print(f"Starting {exe.name}...")
        proc = subprocess.Popen([str(exe)])
        time.sleep(2)

        test_file = Path(__file__).parent / "test_singleton.py"
        rc = subprocess.call(
            [sys.executable, "-m", "pytest", str(test_file), "-v"],
        )

    finally:
        if proc:
            proc.terminate()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()

    sys.exit(rc)


if __name__ == "__main__":
    main()
