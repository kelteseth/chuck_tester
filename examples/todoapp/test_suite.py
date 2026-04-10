"""Test runner for the todoapp example.

Handles start, test execution, and cleanup — like QTest's
initTestCase / cleanupTestCase.

Usage:
    uv run examples/todoapp/test_suite.py
"""

import os
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SAVE_FILE = Path(os.environ.get("LOCALAPPDATA", "")) / "chuck" / "todoapp" / "todos.json"


def find_exe() -> Path:
    env = os.environ.get("TODOAPP_EXE")
    if env:
        return Path(env)
    candidates = sorted(REPO.glob("build/*/examples/todoapp/todoapp.exe"))
    if not candidates:
        sys.exit("todoapp.exe not found — build the project first")
    return candidates[-1]


def main():
    exe = find_exe()
    proc = None
    rc = 1

    try:
        # initTestCase
        SAVE_FILE.unlink(missing_ok=True)
        print(f"Starting {exe.name}...")
        proc = subprocess.Popen([str(exe)])
        time.sleep(2)

        # run tests
        test_file = Path(__file__).parent / "test_todoapp.py"
        rc = subprocess.call(
            [sys.executable, "-m", "pytest", str(test_file), "-v"],
        )

    finally:
        # cleanupTestCase
        if proc:
            proc.terminate()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
        SAVE_FILE.unlink(missing_ok=True)

    sys.exit(rc)


if __name__ == "__main__":
    main()
