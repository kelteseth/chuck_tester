---
name: testa-mcp-explorer
description: 'Use testa MCP server for AI-driven Qt/QML app exploration, test generation, and interactive debugging. Use when: setting up the MCP server, having an AI explore a Qt app, generating tests from MCP sessions, recording during exploration, debugging app behavior interactively.'
---

# AI-Driven Testing with Testa MCP Server

## When to Use

Use this skill when:
- Setting up testa as an MCP server for AI integration
- An AI agent needs to explore and interact with a Qt/QML application
- Generating test scripts from AI exploration sessions
- Recording interactions for later replay or test creation
- Debugging UI behavior by having an AI inspect the QML tree

## MCP Server Overview

Testa provides a FastMCP server that exposes all chuck commands as MCP tools. An AI agent (GitHub Copilot, Claude, or any MCP-compatible client) can connect to this server and interact with a running Qt/QML app.

### Available MCP Tools

| Tool | Purpose | Key Parameters |
|------|---------|---------------|
| `chuck_tree` | Dump the QML visual tree | `depth` |
| `chuck_find` | Find items matching criteria | `objectName`, `text`, `interactive` |
| `chuck_get_property` | Read a QML property | `objectName`, `property` |
| `chuck_click` | Click an item | `objectName` |
| `chuck_keys` | Type text into focused item | `text` |
| `chuck_shortcut` | Fire a keyboard shortcut | `sequence` (e.g. `"Ctrl+S"`) |
| `chuck_wait_property` | Wait for a property value | `objectName`, `property`, `value`, `timeout` |
| `chuck_screenshot` | Capture the screen | — |
| `chuck_drag` | Drag from one point to another | `startX`, `startY`, `endX`, `endY` |
| `chuck_swipe` | Swipe gesture | `objectName`, `direction` |
| `chuck_quit` | Close the application | — |
| `chuck_start_recording` | Begin recording interactions | — |
| `chuck_stop_recording` | Stop and return JSONL | — |

## Setting Up the MCP Server

### 1. Configuration

Add to your MCP client configuration (e.g., VS Code `settings.json` or Claude Desktop config):

```json
{
  "mcpServers": {
    "testa": {
      "command": "uv",
      "args": ["run", "testa", "mcp", "--port", "1408"]
    }
  }
}
```

### 2. Start the Target App

The Qt/QML app must be running with chuck enabled:

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
4. Export JSONL to Python test    → Use testa export CLI
```

The JSONL recording captures every command and response, which can be exported to a Python test:

```bash
uv run testa export recording.jsonl > test_generated.py
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
uv run testa export session.jsonl > tests/test_recorded.py
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
| AI explores, then writes tests | `testa-mcp-explorer` + `testa-testing` |
| AI explores, test fails, AI fixes | `testa-mcp-explorer` + `testa-repair` |
| AI captures baselines during exploration | `testa-mcp-explorer` + `testa-visual` |
| Setting up a new app for AI testing | `chuck-integration` + `testa-mcp-explorer` |

## Troubleshooting

| Problem | Cause | Fix |
|---------|-------|-----|
| MCP server won't start | Port mismatch or app not running | Verify app is running, check port number |
| `chuck_tree` returns empty | ChuckServer not started in app | Check C++ integration (see chuck-integration skill) |
| `chuck_click` fails with "not found" | objectName missing or wrong page | Use `chuck_tree` to find correct names |
| `chuck_click` fails with "not visible" | Item on a different page or hidden | Navigate first, or check item's `visible` property |
| Recording is empty | Forgot to call `start_recording` | Call `chuck_start_recording()` before interactions |
| Exported test has no waits | Exporter doesn't add smart waits | Manually add `wait_for_property` calls for navigation |
