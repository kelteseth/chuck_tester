# Testa MCP Server

Testa can run as an [MCP (Model Context Protocol)](https://modelcontextprotocol.io/) server, exposing chuck commands as tools. This lets any MCP-compatible AI agent (Claude, Copilot, etc.) drive your Qt/QML application directly.

## Architecture

```
AI Agent ←→ MCP (stdio) ←→ testa mcp_server ←→ WebSocket ←→ chuck (in app)
```

## Starting the Server

```bash
uv run testa mcp --port 1408
```

With recording:

```bash
uv run testa mcp --port 1408 --record session.jsonl
```

## MCP Configuration

Add to your MCP client configuration (e.g., VS Code `settings.json` or Claude Desktop config):

```json
{
    "mcpServers": {
        "chuck": {
            "command": "uv",
            "args": ["run", "testa", "mcp", "--port", "1408"]
        }
    }
}
```

## Available Tools

| MCP Tool | Description |
|----------|-------------|
| `chuck_find` | Search the QML tree for items |
| `chuck_get` | Read properties of an item |
| `chuck_click` | Click an item |
| `chuck_keys` | Type text / send key / trigger shortcut |
| `chuck_swipe` | Swipe gesture |
| `chuck_screenshot` | Capture screenshot (returned as image) |
| `chuck_wait` | Wait for item or property |
| `chuck_tree` | Dump the QML tree |

## AI Workflows

### Interactive Testing

The AI connects, explores the tree with `chuck_tree`, then interacts with items it discovers. This is how tests are initially created — the AI learns the UI structure and writes test steps.

### Test Generation

1. AI explores the app via MCP tools
2. Each interaction is recorded to JSONL (with `--record`)
3. Export to a Python test: `uv run testa export session.jsonl --to-python test_flow.py`

### Test Repair

When a test fails:

1. The AI reads the failure report (`<session>.report.json`)
2. Connects via MCP to inspect the current UI state
3. Takes a screenshot to see what changed
4. Dumps the tree to find renamed/moved items
5. Updates the test file

## Recording

When `--record session.jsonl` is passed, every MCP tool call is automatically logged as a JSONL line. The AI doesn't need to know about recording — it just interacts normally.

```jsonl
{"ts": "2026-04-10T14:32:00.000Z", "action": "wait", "item": "mainWindow", "property": "visible", "value": true, "timeout": 10000}
{"ts": "2026-04-10T14:32:01.500Z", "action": "click", "item": "createProjectBtn"}
{"ts": "2026-04-10T14:32:02.300Z", "action": "keys", "item": "projectNameField", "text": "My Test Project"}
```
