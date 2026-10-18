# Chuck CLI Reference

All commands are run via `uv run chuck <command>` or `python -m chuck <command>`.

## replay

Replay a JSONL session file step by step.

```bash
uv run chuck replay session.jsonl [OPTIONS]
```

| Option | Default | Description |
|--------|---------|-------------|
| `--host` | `127.0.0.1` | Tester server host |
| `--port` | `1408` | Tester server port |
| `--baseline-dir` | `<session_dir>/baselines` | Baseline screenshot directory |
| `--fail-on-diff` | off | Exit with error on visual mismatch |

On failure, writes a structured report to `<session>.report.json` with step results, tree dump, and screenshot.

## repair

Replay with enhanced failure context for AI-assisted repair.

```bash
uv run chuck repair session.jsonl [OPTIONS]
```

| Option | Default | Description |
|--------|---------|-------------|
| `--host` | `127.0.0.1` | Tester server host |
| `--port` | `1408` | Tester server port |
| `--report` | `<session>.report.json` | Output path for repair report |

On failure, captures:

- Step-level results (which passed, which failed)
- Error code and message
- QML tree dump at the point of failure
- Screenshot of the app's current state

The report is structured JSON that an AI agent can consume to diagnose and fix the issue.

## repl

Interactive REPL for manual testing.

```bash
uv run chuck repl [OPTIONS]
```

```
chuck> click saveBtn
chuck> get nameField props=text
chuck> wait listPage property=visible value=True
chuck> tree depth=3
chuck> quit
```

## export

Convert a JSONL session to a Python test file.

```bash
uv run chuck export session.jsonl --to-python test_flow.py
```

## baseline

Capture baseline screenshots by replaying a session.

```bash
uv run chuck baseline session.jsonl [OPTIONS]
```

Tagged screenshots that don't have baselines yet will be created automatically.

## verify

Compare screenshots against baselines (CI mode).

```bash
uv run chuck verify session.jsonl [OPTIONS]
```

| Option | Default | Description |
|--------|---------|-------------|
| `--method` | `ssim` | `ssim`, `phash`, or `exact` |
| `--threshold` | `0.95` | Minimum similarity score |

Exits with code 1 on any visual mismatch.

## mcp

Start chuck as an MCP server (stdio transport) for AI agents.

```bash
uv run chuck mcp [OPTIONS]
```

| Option | Default | Description |
|--------|---------|-------------|
| `--host` | `127.0.0.1` | Tester server host |
| `--port` | `1408` | Tester server port |
| `--record` | — | Record all tool calls to this JSONL file |

See [MCP Server](mcp.md) for details.

## profile

Record a QML profiler trace from an app started with `-qmljsdebugger=port:N,host:127.0.0.1,...`. Recording covers only the time until Enter (or `--duration`), and a summary is printed afterwards.

```bash
uv run chuck profile --qml-port 51234 -o trace.qtd [--duration 10]
```

| Option | Default | Description |
|--------|---------|-------------|
| `--qml-port` | required | The app's QML debug port |
| `--qml-host` | `127.0.0.1` | The app's host |
| `--output`, `-o` | required | Output `.qtd` file |
| `--include` | QML features | qmlprofiler features, comma separated |
| `--duration` | until Enter | Seconds to record |
| `--qt-bin` | `$CHUCK_QT_BIN`, `PATH` | Qt bin directory |

## qtd

Summarise a `.qtd` trace: self time by type, the costliest events with file:line, and long top-level ranges.

```bash
uv run chuck qtd trace.qtd [--from 4.2] [--to 9.8] [--top 40] [--long-ms 40]
```

## preview

Run an app under `qmlpreview` so saved QML edits apply to the running app. Enter restarts the app (input replayed), Ctrl+C quits.

```bash
uv run chuck preview --build-dir build/release build/release/myapp.exe --tester-port=1408
```

| Option | Default | Description |
|--------|---------|-------------|
| `--build-dir` | — | Build dir; maps `qrc:` URLs back to the sources |
| `--resource` | — | Extra `.qrc` mapping (repeatable) |
| `--qt-bin` | from build dir, `$CHUCK_QT_BIN`, `PATH` | Qt bin directory |

See [Profiling, Debugging, Inspecting and Live Preview](../guide/qml-tooling.md).
