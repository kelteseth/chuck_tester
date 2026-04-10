# Chuck — Python Controller

Chuck is the Python side of chuck_tester. It connects to a tester-enabled app over WebSocket and provides:

- **Async Python API** — `ChuckClient` for use in tests and scripts
- **CLI** — `chuck replay`, `chuck repl`, `chuck export`, `chuck mcp`, etc.
- **JSONL recording** — capture every action for deterministic replay
- **Screenshot baselines** — save, compare, and diff screenshots
- **MCP server** — expose tester commands as MCP tools for AI agents

## Installation

Chuck is a Python package managed with uv:

```bash
# From the chuck_tester repository root
uv add chuck --editable chuck/

# Or in a standalone project
uv pip install path/to/chuck
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
from chuck import ChuckClient

async with ChuckClient(port=1408) as app:
    await app.wait("mainPage", property="visible", value=True)
    await app.click("addBtn")
    await app.keys("nameField", text="Hello")
    props = await app.get("nameField", props="text")
    print(props["text"])  # "Hello"
    await app.quit()
```

## Package Structure

```
chuck/
├── pyproject.toml
└── chuck/
    ├── __init__.py        # Public API exports
    ├── __main__.py        # CLI entry point
    ├── client.py          # Low-level WebSocket JSON-RPC client
    ├── commands.py        # ChuckClient high-level API
    ├── recorder.py        # JSONL recording
    ├── replayer.py        # JSONL replay with structured reporting
    ├── exporter.py        # JSONL → Python test conversion
    ├── screenshots.py     # Baseline management
    ├── visual_diff.py     # SSIM, pHash, diff image generation
    ├── mcp_server.py      # MCP tool server for AI agents
    └── repl.py            # Interactive REPL
```
