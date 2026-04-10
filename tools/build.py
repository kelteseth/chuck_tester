"""Configure and build the chuck_tester project using CMake presets.

Automates the build pipeline:
  1. Configures cmake (via presets) if build dir doesn't exist or --configure is set
  2. Builds with cmake, tee'ing output to build.log in the build dir

On Windows, automatically finds and activates VS Developer Shell before
cmake configure and build steps.

Examples:
    # Windows MSVC debug build
    uv run tools/build.py --preset windows-debug-qt-6.11.0

    # Linux GCC debug build
    uv run tools/build.py --preset linux-debug-qt-6.11.0

    # macOS Clang release, specific target
    uv run tools/build.py --preset macos-release-qt-6.11.0 --target tester

    # Force reconfigure
    uv run tools/build.py --preset windows-debug-qt-6.11.0 --configure
"""

from __future__ import annotations

import argparse
import json
import re
import shlex
import subprocess
import sys
from pathlib import Path

_SAFE_ARG_RE = re.compile(r"^[a-zA-Z0-9._\-]+$")

SCRIPT_DIR: Path = Path(__file__).resolve().parent
REPO_ROOT: Path = SCRIPT_DIR.parent

# Maps configure preset -> build preset (loaded from CMakePresets.json)
_BUILD_PRESET_MAP: dict[str, str] = {}


def _ps_quote(path: Path | str) -> str:
    """Quote a path for PowerShell single-quote interpolation."""
    return "'" + str(path).replace("'", "''") + "'"


def _sh_quote(path: Path | str) -> str:
    """Quote a path for bash interpolation."""
    return shlex.quote(str(path))


def find_vsdevshell() -> Path | None:
    """Find the VS Developer Shell launcher script.

    Searches for Launch-VsDevShell.ps1 in the standard Visual Studio 2022
    installation directories.
    """
    import glob

    pattern: str = r"C:\Program Files\Microsoft Visual Studio\2022\*\Common7\Tools\Launch-VsDevShell.ps1"
    matches: list[str] = glob.glob(pattern)
    return Path(matches[0]) if matches else None


def run_powershell(script: str) -> int:
    """Execute a PowerShell script and return its exit code."""
    try:
        result = subprocess.run(
            ["pwsh.exe", "-NoProfile", "-Command", script],
            cwd=str(REPO_ROOT),
        )
        return result.returncode
    except OSError as exc:
        print(f"ERROR: failed to launch pwsh.exe: {exc}", file=sys.stderr)
        return 1


def load_presets() -> dict:
    """Load CMakePresets.json and build the preset mapping."""
    presets_path = REPO_ROOT / "CMakePresets.json"
    with open(presets_path) as f:
        data = json.load(f)

    for bp in data.get("buildPresets", []):
        cfg = bp.get("configurePreset")
        if cfg:
            _BUILD_PRESET_MAP[cfg] = bp["name"]

    return data


def resolve_build_dir(presets_data: dict, configure_preset: str) -> Path | None:
    """Resolve the build directory from the configure preset's binaryDir."""
    for cp in presets_data.get("configurePresets", []):
        if cp["name"] == configure_preset:
            binary_dir = cp.get("binaryDir")
            if binary_dir:
                resolved = binary_dir.replace("${sourceDir}", str(REPO_ROOT))
                return Path(resolved)
    return None


def run_configure(
    preset: str,
    vsdevshell: Path | None,
) -> int:
    """Run cmake configure step."""
    print(f"=== Configuring: cmake --preset {preset} ===", file=sys.stderr)

    if sys.platform == "win32":
        cmake_cmd = f"cmake --preset {_ps_quote(preset)}"
        ps_script = (
            f"& {_ps_quote(vsdevshell)} -Arch amd64 -SkipAutomaticLocation; "
            f"{cmake_cmd} 2>&1; exit $LASTEXITCODE"
        )
        rc = run_powershell(ps_script)
    else:
        cmake_cmd = f"cmake --preset {_sh_quote(preset)}"
        rc = subprocess.run(
            ["bash", "-c", cmake_cmd], cwd=str(REPO_ROOT)
        ).returncode

    if rc != 0:
        print(f"ERROR: cmake configure failed (exit code {rc})", file=sys.stderr)
    else:
        print("=== Configure complete ===", file=sys.stderr)
    return rc


def run_build(
    build_preset: str,
    build_dir: Path,
    target: str | None,
    vsdevshell: Path | None,
) -> int:
    """Run cmake build step with output tee'd to build log."""
    build_log = build_dir / "build.log"
    target_args = ["--target", target] if target else []
    target_str = " ".join(target_args)
    print(
        f"=== Building: cmake --build --preset {build_preset} {target_str} ===",
        file=sys.stderr,
    )

    if sys.platform == "win32":
        cmake_cmd = " ".join(
            ["cmake", "--build", "--preset", _ps_quote(build_preset)] + target_args
        )
        ps_script = (
            f"& {_ps_quote(vsdevshell)} -Arch amd64 -SkipAutomaticLocation; "
            f"{cmake_cmd} 2>&1 | Tee-Object -FilePath {_ps_quote(build_log)}; "
            f"exit $LASTEXITCODE"
        )
        rc = run_powershell(ps_script)
    else:
        cmake_cmd = " ".join(
            ["cmake", "--build", "--preset", _sh_quote(build_preset)] + target_args
        )
        cmd = f"{cmake_cmd} 2>&1 | tee {_sh_quote(build_log)}"
        rc = subprocess.run(["bash", "-c", cmd], cwd=str(REPO_ROOT)).returncode

    print(f"\n=== Build finished (exit code {rc}) ===", file=sys.stderr)
    print(f"Build log: {build_log}", file=sys.stderr)
    return rc


def create_parser() -> argparse.ArgumentParser:
    """Create the argument parser."""
    parser = argparse.ArgumentParser(
        description="Configure and build chuck_tester using CMake presets.",
        epilog=(
            "Available presets:\n"
            "  windows-debug-qt-6.11.0   MSVC 64-bit Debug\n"
            "  windows-release-qt-6.11.0  MSVC 64-bit Release\n"
            "  linux-debug-qt-6.11.0     GCC 64-bit Debug\n"
            "  linux-release-qt-6.11.0    GCC 64-bit Release\n"
            "  macos-debug-qt-6.11.0     Clang 64-bit Debug\n"
            "  macos-release-qt-6.11.0    Clang 64-bit Release\n"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--preset",
        required=True,
        help="CMake configure preset name (e.g. windows-debug-qt-6.11.0)",
    )
    parser.add_argument(
        "--target",
        default=None,
        help="Build target (e.g. tester). Omit to build all targets.",
    )
    parser.add_argument(
        "--configure",
        action="store_true",
        help="Force cmake reconfigure even if build dir exists.",
    )
    return parser


def main() -> int:
    """Run the build pipeline: configure (if needed), build."""
    args = create_parser().parse_args()

    # Validate args that get interpolated into shell commands
    for name, value in [("--preset", args.preset), ("--target", args.target)]:
        if value is not None and not _SAFE_ARG_RE.match(value):
            print(f"ERROR: {name} contains unsafe characters: {value!r}", file=sys.stderr)
            return 1

    presets_data = load_presets()

    # Resolve build preset from configure preset
    build_preset = _BUILD_PRESET_MAP.get(args.preset)
    if not build_preset:
        print(
            f"ERROR: no build preset found for configure preset '{args.preset}'",
            file=sys.stderr,
        )
        print(
            f"Available configure presets: {', '.join(_BUILD_PRESET_MAP.keys())}",
            file=sys.stderr,
        )
        return 1

    build_dir = resolve_build_dir(presets_data, args.preset)
    if not build_dir:
        print(f"ERROR: could not resolve binaryDir for preset '{args.preset}'", file=sys.stderr)
        return 1

    # Find VS Developer Shell on Windows
    vsdevshell: Path | None = None
    if sys.platform == "win32":
        vsdevshell = find_vsdevshell()
        if not vsdevshell:
            print("ERROR: Could not find Launch-VsDevShell.ps1", file=sys.stderr)
            print(
                "Install Visual Studio 2022 or set the path manually.",
                file=sys.stderr,
            )
            return 1
        print(f"Using VS Dev Shell: {vsdevshell}", file=sys.stderr)

    # Step 1: Configure (if needed)
    needs_configure = not build_dir.exists() or args.configure
    if needs_configure:
        rc = run_configure(args.preset, vsdevshell)
        if rc != 0:
            return rc
    else:
        print(
            f"=== Build dir exists ({build_dir}), skipping configure (use --configure to force) ===",
            file=sys.stderr,
        )

    # Step 2: Build
    return run_build(build_preset, build_dir, args.target, vsdevshell)


if __name__ == "__main__":
    sys.exit(main())
