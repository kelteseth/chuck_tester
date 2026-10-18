# chuck_tester — Agent Context

AI-driven UI testing for Qt/QML. **tester** is a C++ static library linked into the app under test; **chuck** is the Python controller that drives it over WebSocket (JSON-RPC 2.0, default port 1408).

GitHub: `github.com/kelteseth/chuck_tester`

## Repo Layout

```
.agent/skills/          Project-level skills (build, deploy)
tester/                 C++ static library (TesterServer, WebSocket server)
  skills/               Skills shipped with tester (integration guide)
chuck/                  Python package (ChuckClient, MCP server, visual diff)
  chuck/                Source: client.py, commands.py, mcp_server.py, …
  skills/               Skills shipped with chuck (testing, repair, visual, MCP)
examples/
  minimal/              Bare-minimum QML app with tester
  todoapp/              Full example with C++ model + tests
  text_editor/          Skeleton for AI-driven building
tools/build.py          Cross-platform CMake build wrapper
CMakePresets.json       Configure/build presets (Windows/Linux/macOS × Debug/Release)
pyproject.toml          Root workspace: chuck + pytest deps
docs/                   MkDocs Material documentation site
```

## Building

```bash
uv run tools/build.py --preset windows-debug-qt-6.11.0          # full build
uv run tools/build.py --preset windows-debug-qt-6.11.0 --target todoapp  # single target
```

On Windows, `tools/build.py` auto-activates the VS Developer Shell. First run after build needs `windeployqt` — see `.agent/skills/cpp-deploy/SKILL.md`.

Presets: `windows-debug-qt-6.11.0`, `windows-release-qt-6.11.0`, `linux-debug-qt-6.11.0`, `linux-release-qt-6.11.0`, `macos-debug-qt-6.11.0`, `macos-release-qt-6.11.0`.

## Running Tests

```bash
# From repo root (uses root pyproject.toml)
uv run pytest examples/todoapp/test_todoapp.py -v

# From an example with its own pyproject.toml
cd examples/text_editor
uv sync
uv run pytest -v
```

The app must be running with tester enabled before tests execute. Tests do NOT launch the app.

## Skills Reference

| Skill | Location | Purpose |
|-------|----------|---------|
| cpp-build | `.agent/skills/cpp-build/` | Build this project with `tools/build.py` and CMake presets |
| cpp-deploy | `.agent/skills/cpp-deploy/` | Deploy Qt runtime DLLs before first launch |
| tester-integration | `tester/skills/tester-integration/` | Add tester to a Qt/QML app, objectName conventions |
| chuck-testing | `chuck/skills/chuck-testing/` | Write tests with ChuckClient, live-validation loop |
| chuck-repair | `chuck/skills/chuck-repair/` | Diagnose and fix test failures |
| chuck-visual | `chuck/skills/chuck-visual/` | Screenshot comparison and visual regression |
| chuck-mcp-explorer | `chuck/skills/chuck-mcp-explorer/` | Use chuck's MCP server for interactive exploration |
| chuck-qml-tooling | `chuck/skills/chuck-qml-tooling/` | Profile, inspect (source locations, item_at), live-preview, attach debuggers |

## Tech Stack

- **C++23**, CMake ≥ 4.2, Ninja, Qt 6.11.0+
- **Python** ≥ 3.12, uv, pytest + pytest-asyncio
- **Windows**: MSVC 2022 — **Linux**: GCC — **macOS**: Clang

## Conventions

- Every QML item a test interacts with gets an `objectName` (see tester-integration skill for naming patterns)
- C++ types exposed to QML use `QML_ELEMENT`
- Tests are async (`pytest-asyncio` with `asyncio_mode = "auto"`)
- WebSocket port 1408 by default
