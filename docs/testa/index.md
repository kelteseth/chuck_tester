# Testa — Python Controller

Testa is the Python side of chuck+testa. It connects to a chuck-enabled app over WebSocket and provides:

- **Async Python API** — `TestaClient` for use in tests and scripts
- **CLI** — `testa replay`, `testa repl`, `testa export`, `testa mcp`, etc.
- **JSONL recording** — capture every action for deterministic replay
- **Screenshot baselines** — save, compare, and diff screenshots
- **MCP server** — expose chuck commands as MCP tools for AI agents

## Installation

Testa is a Python package managed with uv:

```bash
# From the chuck repository root
uv add testa --editable testa/

# Or in a standalone project
uv pip install path/to/testa
```

### Dependencies

| Package | Purpose |
|---------|---------|
| `websockets` | WebSocket client |
| `Pillow` | Screenshot handling, image comparison |
| `mcp` | MCP server (optional, for AI integration) |
| `pytest` + `pytest-asyncio` | Test framework (dev dependency) |

## Quick Example

```python
from testa import TestaClient

async with TestaClient(port=1408) as app:
    await app.wait("mainPage", property="visible", value=True)
    await app.click("addBtn")
    await app.keys("nameField", text="Hello")
    props = await app.get("nameField", props="text")
    print(props["text"])  # "Hello"
    await app.quit()
```

## Package Structure

```
testa/
├── pyproject.toml
└── testa/
    ├── __init__.py        # Public API exports
    ├── __main__.py        # CLI entry point
    ├── client.py          # Low-level WebSocket JSON-RPC client
    ├── commands.py        # TestaClient high-level API
    ├── recorder.py        # JSONL recording
    ├── replayer.py        # JSONL replay with structured reporting
    ├── exporter.py        # JSONL → Python test conversion
    ├── screenshots.py     # Baseline management
    ├── visual_diff.py     # SSIM, pHash, diff image generation
    ├── mcp_server.py      # MCP tool server for AI agents
    └── repl.py            # Interactive REPL
```
