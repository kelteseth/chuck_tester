# Text Editor — AI-Built Example

This is a **barebone Qt/QML skeleton** with tester already wired in. The idea: point an AI agent at this folder and let it build a complete text editor using the chuck_tester MCP tools to explore, build, and test the UI iteratively.

## What's Included

| File | Purpose |
|------|---------|
| `CMakeLists.txt` | Build config with tester linked |
| `main.cpp` | App entry point with `tester::TesterServer` on port 1408 |
| `Main.qml` | Empty `ApplicationWindow` — your canvas |

## Prerequisites

1. Build the project so the skeleton compiles:
   ```bash
   uv run build.py --preset windows-debug-qt-6.11.0
   ```
2. Start the app:
   ```bash
   ./build/MSVC_64bit_Debug/examples/text_editor/text_editor.exe
   ```
3. Connect the AI via MCP — add to your MCP config:
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

## The Prompt

Copy the prompt below into VS Code Copilot (agent mode), Claude Code, or any MCP-capable AI. The AI will use the chuck MCP tools to explore the running app, edit files, rebuild, and verify — iteratively building the full text editor.

---

### Prompt: Build a Text Editor

> You are building a Qt 6 / QML text editor app. The skeleton is in `examples/text_editor/`. It already has `main.cpp` with `tester::TesterServer` wired up on port 1408 and an empty `Main.qml` window.
>
> Use the chuck MCP tools (`chuck_tree`, `chuck_find`, `chuck_click`, `chuck_keys`, `chuck_screenshot`, etc.) to explore the running app after each change.
>
> **Step 1 — C++ backend class**
>
> Create a `FileManager` class (`FileManager.h` / `FileManager.cpp`) exposed to QML:
>
> - Properties: `filePath` (QString), `content` (QString), `modified` (bool), `fileName` (read-only, derived from filePath)
> - Slots: `newFile()`, `open(const QString &path)`, `save()`, `saveAs(const QString &path)`
> - Signal: `fileLoaded()`, `fileSaved()`
> - Use `Q_PROPERTY` with `NOTIFY` signals. Register with `QML_ELEMENT`.
> - Read/write files using `QFile` + `QTextStream`.
>
> Register the type in `main.cpp` before loading QML. Add the files to `CMakeLists.txt`.
>
> **Step 2 — QML UI**
>
> Build the UI in `Main.qml` using Material Design (`QtQuick.Controls.Material`):
>
> - A `MenuBar` with File menu: New, Open, Save, Save As, Quit
> - A `TextArea` filling the window, bound to `FileManager.content`
> - A status bar at the bottom showing the file name and modified indicator
> - File dialogs (`Qt.labs.platform` or `QtQuick.Dialogs`) for Open and Save As
> - Give every interactive element an `objectName` (e.g. `"textArea"`, `"menuFile"`, `"actionNew"`, `"statusLabel"`)
>
> **Step 3 — Verify with MCP**
>
> After each change, rebuild and use the MCP tools:
> ```
> chuck_tree depth=3        — verify the QML structure
> chuck_find interactive=True — check all named elements exist
> chuck_screenshot           — see the visual result
> chuck_click "actionNew"   — test menu actions
> chuck_keys "textArea" text="Hello world"  — test typing
> chuck_get_property "statusLabel" "text"   — verify status updates
> ```
>
> **Step 4 — Write a test**
>
> Create `test_text_editor.py` in this folder:
> ```python
> import pytest
> from chuck import ChuckClient
>
> @pytest.mark.asyncio
> async def test_new_file_type_save():
>     async with ChuckClient(port=1408) as app:
>         await app.wait("mainWindow", property="visible", value=True, timeout=5000)
>         # Type some text
>         await app.click("textArea")
>         await app.keys("textArea", text="Hello from chuck_tester!")
>         # Verify content
>         props = await app.get("statusLabel", props="text")
>         assert "Modified" in props["text"] or "modified" in props["text"].lower()
>         await app.quit()
> ```
>
> Run the test: `uv run pytest examples/text_editor/test_text_editor.py -v`

---

## Skills Available

The AI should use these skills (loaded automatically from `.github/skills/`):

| Skill | What it teaches the AI |
|-------|----------------------|
| `tester-integration` | How to wire tester into a Qt app, objectName conventions |
| `chuck-testing` | How to write pytest async tests with `ChuckClient` |
| `chuck-mcp-explorer` | How to explore and interact with an app via MCP tools |
| `chuck-repair` | How to diagnose and fix failing tests |
| `chuck-visual` | How to capture and compare screenshots |
