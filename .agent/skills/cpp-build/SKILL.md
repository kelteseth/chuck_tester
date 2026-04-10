---
name: cpp-build
description: 'Build the chuck_tester C++ project and its examples. Use when: compiling tester or example apps, running cmake, resolving build errors, setting up the build environment on Windows/Linux/macOS.'
---

# Building chuck_tester (C++)

## Prerequisites

- **CMake** ≥ 4.2 and **Ninja**
- **Qt** 6.11.0+ (Quick, Qml, QuickControls2, WebSockets, Test modules)
- **C++23** compiler: MSVC 2022 (Windows), GCC (Linux), Clang (macOS)
- **Python** ≥ 3.12 and **uv** (to run `build.py`)

## Quick Build

The repo includes `build.py` which wraps cmake configure + build and handles the VS Developer Shell on Windows automatically.

```bash
# Windows
uv run build.py --preset windows-debug-qt-6.11.0

# Linux
uv run build.py --preset linux-debug-qt-6.11.0

# macOS
uv run build.py --preset macos-debug-qt-6.11.0
```

### Build a single target

```bash
uv run build.py --preset windows-debug-qt-6.11.0 --target text_editor
```

### Force reconfigure

```bash
uv run build.py --preset windows-debug-qt-6.11.0 --configure
```

## Available Presets

| Preset | Platform | Config |
|--------|----------|--------|
| `windows-debug-qt-6.11.0` | Windows MSVC 64-bit | Debug |
| `windows-release-qt-6.11.0` | Windows MSVC 64-bit | Release |
| `linux-debug-qt-6.11.0` | Linux GCC 64-bit | Debug |
| `linux-release-qt-6.11.0` | Linux GCC 64-bit | Release |
| `macos-debug-qt-6.11.0` | macOS Clang 64-bit | Debug |
| `macos-release-qt-6.11.0` | macOS Clang 64-bit | Release |

## What build.py Does

1. Loads `CMakePresets.json` to resolve the build directory and build preset
2. On Windows, finds and activates the VS Developer Shell (`Launch-VsDevShell.ps1`)
3. Runs `cmake --preset <name>` to configure (skipped if build dir already exists)
4. Runs `cmake --build --preset <name>` and tee's output to `build.log`

## Build Output Locations

| Preset | Build directory |
|--------|----------------|
| Windows Debug | `build/MSVC_64bit_Debug/` |
| Windows Release | `build/MSVC_64bit_Release/` |
| Linux Debug | `build/gcc_64_Debug/` |
| Linux Release | `build/gcc_64_Release/` |
| macOS Debug | `build/clang_64_Debug/` |
| macOS Release | `build/clang_64_Release/` |

Example executables land inside the build directory, e.g.:
```
build/MSVC_64bit_Debug/examples/text_editor/text_editor.exe
build/MSVC_64bit_Debug/examples/todoapp/todoapp.exe
```

## Project Structure

```
CMakeLists.txt          # Root — adds tester + all examples
├── tester/             # C++ static library (TesterServer)
├── examples/
│   ├── minimal/        # Bare-minimum QML app with tester
│   ├── todoapp/        # Full example with tests
│   └── text_editor/    # Skeleton for AI-driven building
└── build.py            # Cross-platform build wrapper
```

The root `CMakeLists.txt` uses `add_subdirectory()` for tester and each example. Each example has its own `CMakeLists.txt` that links against `tester` and the required Qt modules.

## Troubleshooting

### "Could not find Launch-VsDevShell.ps1" (Windows)
Install Visual Studio 2022 with the "Desktop development with C++" workload.

### Qt not found
Verify `CMAKE_PREFIX_PATH` in `CMakePresets.json` points to your Qt installation. Default Windows path: `C:/Qt/6.11.0/msvc2022_64`.

### Build log
Check `build/<preset>/build.log` for the full compiler output.

## Running the Executable

**First run: deploy Qt dependencies before launching.** Run windeployqt once after each build that adds new Qt modules (see cpp-deploy skill). Without this, QML plugin imports will fail silently and the app will exit immediately.

After deploying, launch the exe directly:

```python
# Recommended: launch from Python so you can check the exit code
import subprocess
exe = r"build\MSVC_64bit_Debug\examples\text_editor\text_editor.exe"
proc = subprocess.Popen([exe])
```

```bash
# Or from bash
./build/MSVC_64bit_Debug/examples/text_editor/text_editor.exe
```

## Debugging Startup Crashes

If the app exits immediately (exit code `-1` / `4294967295`), it's almost always a QML load failure. On Windows, Qt GUI apps have no console so output is invisible by default. Capture it by setting `QT_FORCE_STDERR_LOGGING=1` and piping stderr:

```python
import subprocess, os, time

exe = r"build\MSVC_64bit_Debug\examples\text_editor\text_editor.exe"
env = {**os.environ, "QT_FORCE_STDERR_LOGGING": "1"}
proc = subprocess.Popen([exe], stderr=subprocess.PIPE, env=env)
time.sleep(2)
if proc.poll() is not None:
    print(proc.stderr.read().decode())
```

Common errors and fixes:

| Error | Fix |
|-------|-----|
| `Cannot assign to non-existent property` | Wrong property name for the QML type |
| `module "X" is not installed` | QML plugin not deployed — run windeployqt |
| `QQmlApplicationEngine failed to load component` | Syntax error or bad import in a QML file |
