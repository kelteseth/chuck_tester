# Testa CLI Reference

All commands are run via `uv run testa <command>` or `python -m testa <command>`.

## replay

Replay a JSONL session file step by step.

```bash
uv run testa replay session.jsonl [OPTIONS]
```

| Option | Default | Description |
|--------|---------|-------------|
| `--host` | `127.0.0.1` | Chuck server host |
| `--port` | `1408` | Chuck server port |
| `--baseline-dir` | `<session_dir>/baselines` | Baseline screenshot directory |
| `--fail-on-diff` | off | Exit with error on visual mismatch |

On failure, writes a structured report to `<session>.report.json` with step results, tree dump, and screenshot.

## repair

Replay with enhanced failure context for AI-assisted repair.

```bash
uv run testa repair session.jsonl [OPTIONS]
```

| Option | Default | Description |
|--------|---------|-------------|
| `--host` | `127.0.0.1` | Chuck server host |
| `--port` | `1408` | Chuck server port |
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
uv run testa repl [OPTIONS]
```

```
testa> click saveBtn
testa> get nameField props=text
testa> wait listPage property=visible value=True
testa> tree depth=3
testa> quit
```

## export

Convert a JSONL session to a Python test file.

```bash
uv run testa export session.jsonl --to-python test_flow.py
```

## baseline

Capture baseline screenshots by replaying a session.

```bash
uv run testa baseline session.jsonl [OPTIONS]
```

Tagged screenshots that don't have baselines yet will be created automatically.

## verify

Compare screenshots against baselines (CI mode).

```bash
uv run testa verify session.jsonl [OPTIONS]
```

| Option | Default | Description |
|--------|---------|-------------|
| `--method` | `ssim` | `ssim`, `phash`, or `exact` |
| `--threshold` | `0.95` | Minimum similarity score |

Exits with code 1 on any visual mismatch.

## mcp

Start testa as an MCP server (stdio transport) for AI agents.

```bash
uv run testa mcp [OPTIONS]
```

| Option | Default | Description |
|--------|---------|-------------|
| `--host` | `127.0.0.1` | Chuck server host |
| `--port` | `1408` | Chuck server port |
| `--record` | — | Record all tool calls to this JSONL file |

See [MCP Server](mcp.md) for details.
