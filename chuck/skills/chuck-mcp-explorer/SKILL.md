---
name: chuck-mcp-explorer
description: 'Use chuck MCP server for AI-driven Qt/QML app exploration, test generation, and interactive debugging. Use when: setting up the MCP server, having an AI explore a Qt app, generating tests from MCP sessions, recording during exploration, debugging app behavior interactively.'
---

# AI-Driven Testing with Chuck MCP Server

## When to Use

Use this skill when:
- Setting up chuck as an MCP server for AI integration
- An AI agent needs to explore and interact with a Qt/QML application
- Generating test scripts from AI exploration sessions
- Recording interactions for later replay or test creation
- Debugging UI behavior by having an AI inspect the QML tree

## MCP Server Overview

Chuck provides a FastMCP server that exposes all chuck commands as MCP tools. An AI agent (GitHub Copilot, Claude, or any MCP-compatible client) can connect to this server and interact with a running Qt/QML app.

### Available MCP Tools

| Tool | Purpose | Key Parameters |
|------|---------|---------------|
| `chuck_tree` | Dump the QML visual tree | `root`, `depth`, `props` |
| `chuck_find` | Find items matching criteria | `type`, `interactive`, `root`, `props` |
| `chuck_get` | Read properties | `item`, `props` |
| `chuck_wait` | Wait for an item or property value | `item`, `property`, `value`, `timeout` |
| `chuck_click` | Click an item | `item`, `button`, `double`, `hold` |
| `chuck_keys` | Type text, send a key or a shortcut | `item`, `text` / `key` / `shortcut` |
| `chuck_drag` | Drag to an item or by an offset | `from_item`, `to_item` / `by`, `steps` |
| `chuck_swipe` | Swipe gesture | `item`, `direction`, `distance` |
| `chuck_hover` | Hover an item (no button) | `item` + `by`, or `point` |
| `chuck_scroll` | Mouse-wheel scroll | `item`, `direction`, `steps`, `modifiers` |
| `chuck_window` | Window state/flags/geometry | `state`, `frameless`, `stay_on_top`, `width`, `height`, `x`, `y` |
| `chuck_set` | Write a property (removes its binding) | `item`, `property`, `value` |
| `chuck_invoke` | Call a QML function / Q_INVOKABLE | `item`, `method`, `args` |
| `chuck_screenshot` | Capture the window or an item | `item`, `tag`, `verify` |
| `chuck_quit` | Close the application | — |

Recording is not a tool: start the server with `--record session.jsonl` and every tool call is logged.

Prefer `chuck_set`/`chuck_invoke` to reach a state quickly (switch theme, open a page) and
real input (`click`, `hover`, ...) for the behavior under test.

## Setting Up the MCP Server

### 1. Configuration

Add to your MCP client configuration (e.g., VS Code `settings.json` or Claude Desktop config):

```json
{
  "mcpServers": {
    "chuck": {
      "command": "uv",
      "args": ["run", "chuck", "mcp", "--port", "1408"]
    }
  }
}
```

### 2. Start the Target App

The Qt/QML app must be running with tester enabled:

```bash
./build/MSVC_64bit_Debug/examples/todoapp/todoapp.exe
```

### 3. Connect

The MCP server connects to the app on server startup. The AI can then call tools immediately.

## AI Exploration Workflows

### Workflow 1: Discover App Structure

The AI should start by understanding the app:

```
1. chuck_tree(depth=3)          → See the full QML tree
2. chuck_find(interactive=True)  → Find all clickable/typeable items
3. chuck_screenshot()            → See what the app looks like
```

This gives the AI a mental model of the app's pages, navigation, and interactive elements.

### Workflow 2: Navigate and Interact

After discovery, the AI can drive the app:

```
1. chuck_click("addBtn")                              → Click a button
2. chuck_wait_property("stack", "busy", False)         → Wait for page transition
3. chuck_tree(depth=2)                                 → See the new page
4. chuck_click("titleField")                           → Focus a text field
5. chuck_keys("Buy groceries")                         → Type text
6. chuck_click("saveBtn")                              → Save
7. chuck_wait_property("stack", "busy", False)         → Wait for navigation back
8. chuck_get_property("label_0", "text")               → Verify the item
```

### Workflow 3: Record and Generate Tests

The AI can record its interactions, then generate a test:

```
1. chuck_start_recording()       → Begin recording
2. [AI explores and interacts]   → Normal interaction
3. chuck_stop_recording()        → Get JSONL recording
4. Export JSONL to Python test    → Use chuck export CLI
```

The JSONL recording captures every command and response, which can be exported to a Python test:

```bash
uv run chuck export recording.jsonl > test_generated.py
```

### Workflow 4: Debug a Failing Test

When a test fails, the AI can reproduce the issue interactively:

```
1. Navigate to the same app state as the failing test
2. chuck_tree(depth=5)           → Full tree dump at failure point
3. chuck_screenshot()            → Visual state
4. chuck_get_property("item", "prop") → Check property values
5. chuck_find(objectName="missing_item") → Verify item existence
```

## Effective Prompts for AI Exploration

### Exploring a New App

> "Connect to the Qt app on port 1408. Explore its UI tree, take a screenshot, and describe all the pages and interactive elements you find."

### Generating a Test

> "The TodoApp is running on port 1408. Record a session where you: add a new todo item called 'Test Item', mark it as complete, then verify it shows as completed. Export the recording as a Python pytest."

### Debugging

> "My test fails at `wait_for_property('label_0', 'text', 'Expected')`. Connect to the app after navigating to the list page and check what `label_0.text` actually contains. Also dump the tree to see if `label_0` exists."

## Recording During MCP Sessions

### Automatic Recording

Start recording at the beginning of an exploration session:

```python
# The AI calls these MCP tools:
chuck_start_recording()
# ... all subsequent commands are recorded ...
chuck_stop_recording()  # Returns JSONL string
```

### JSONL Format

Each recorded line is:
```json
{"method": "click", "params": {"objectName": "addBtn"}, "timestamp": 1234567890.123}
{"method": "keys", "params": {"text": "Buy groceries"}, "timestamp": 1234567890.456}
```

### Exporting to Python

```bash
# Convert JSONL to a Python test
uv run chuck export session.jsonl > tests/test_recorded.py
```

The exported test includes proper async/await, fixture setup, and wait calls.

## Test Generation Best Practices

When an AI generates tests from exploration:

1. **Add explicit waits** — Don't rely on timing. Use `wait_for_property()` after navigation and data operations.

2. **Use objectName, not coordinates** — Click by name, not position. Coordinates break on different screen sizes.

3. **Assert after actions** — After clicking save, verify the data appears. After deleting, verify it's gone.

4. **Clean up state** — If the test creates data, delete it. Or use a fresh app instance per test.

5. **Group by feature** — One test file per feature area (e.g., `test_add_todo.py`, `test_delete_todo.py`).

## Combining with Other Skills

| Task | Skills to Combine |
|------|-------------------|
| AI explores, then writes tests | `chuck-mcp-explorer` + `chuck-testing` |
| AI explores, test fails, AI fixes | `chuck-mcp-explorer` + `chuck-repair` |
| AI captures baselines during exploration | `chuck-mcp-explorer` + `chuck-visual` |
| Setting up a new app for AI testing | `tester-integration` + `chuck-mcp-explorer` |

## Troubleshooting

| Problem | Cause | Fix |
|---------|-------|-----|
| MCP server won't start | Port mismatch or app not running | Verify app is running, check port number |
| `chuck_tree` returns empty | TesterServer not started in app | Check C++ integration (see tester-integration skill) |
| `chuck_click` fails with "not found" | objectName missing or wrong page | Use `chuck_tree` to find correct names |
| `chuck_click` fails with "not visible" | Item on a different page or hidden | Navigate first, or check item's `visible` property |
| Recording is empty | Forgot to call `start_recording` | Call `chuck_start_recording()` before interactions |
| Exported test has no waits | Exporter doesn't add smart waits | Manually add `wait_for_property` calls for navigation |
