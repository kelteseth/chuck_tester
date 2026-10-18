<p align="center">
  <img src="docs/assets/logo.svg" alt="chuck_tester" width="600">
</p>

> [!WARNING]
> **Experimental.** chuck_tester is an experimental tool for automated GUI testing — driven either by hand-written tests or by an AI agent. The API is unstable and may change without notice.
>
> It is currently being tested in [ScreenPlay](https://screen-play.app/), an Open Source Live-Wallpaper app for Windows and OSX.

UI testing for Qt/QML apps. Tester is a C++ library you link into your app. Chuck is the Python side that talks to it.

Writing GUI tests by hand is a hassle — clicking through the UI to collect objectNames, guessing what to wait for, rewriting half the suite whenever the layout moves. chuck_tester exists so an AI agent can take that work off you: it drives the running app, discovers the UI itself, then writes and repairs the tests. You can still write every test by hand — the API is plain pytest — but that isn't what it's built for. See [Use it with an AI agent](#use-it-with-an-ai-agent).

## Quick start

Add tester to your app:

```cpp
#include <tester/TesterServer.h>

// after your QML engine loads:
auto *window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
tester::TesterServer server(*window);
server.start();
```

Link it in CMake:

```cmake
find_package(Qt6 REQUIRED COMPONENTS Quick Qml WebSockets Test)
target_link_libraries(myapp PRIVATE Qt6::Quick Qt6::Qml tester)
```

Write a test:

```python
import pytest
from chuck import ChuckClient

@pytest.mark.asyncio
async def test_my_app():
    async with ChuckClient(port=1408) as app:
        await app.wait("mainWindow", property="visible", value=True, timeout=5000)
        await app.click("myButton")
        props = await app.get("myLabel", props="text")
        assert props["text"] == "Hello"
        await app.quit()
```

Run it (app must be running first):

```bash
uv run pytest examples/todoapp/test_todoapp.py -v
```

## What chuck can do

- `click` / `keys` / `drag` / `swipe` / `hover` / `scroll` — interact with QML items by objectName
- `set` / `invoke` — write properties and call QML functions to jump straight to a state
- `window` — maximize, fullscreen, frameless, resize, move
- `get` — read any property
- `wait` — wait for items to appear or properties to change
- `find` / `tree` — search and inspect the QML tree, with each item's QML `file:line`
- `item_at` — pick what is drawn at a point, like an inspector
- `screenshot` — capture PNGs, compare against baselines (SSIM, pHash, exact)
- `quit` — close the app
- QML profiler, QML debugger and live preview (`qmlpreview`) attach without a `QT_QML_DEBUG` build — see the [guide](docs/guide/qml-tooling.md)

## Use it with an AI agent

chuck was built for AI-driven testing: the agent drives your running app, sees what it renders, and writes or repairs tests from what it finds.

### Claude Code

Installing the plugin brings the skills and the MCP server in one step:

```
/plugin marketplace add kelteseth/chuck_tester
/plugin install chuck@chuck-tester
/plugin install tester@chuck-tester
```

That adds five skills, which the agent loads on its own when they're relevant:

| Skill | Covers |
|-------|--------|
| `chuck:chuck-testing` | Writing tests, the live-validation loop |
| `chuck:chuck-repair` | Diagnosing and fixing failing tests |
| `chuck:chuck-visual` | Screenshot baselines, SSIM/pHash thresholds, masks |
| `chuck:chuck-mcp-explorer` | Driving the app interactively over MCP |
| `tester:tester-integration` | Adding tester to an app, objectName conventions |

### Any other MCP client

chuck exposes the tester commands as MCP tools over stdio, so any MCP-capable agent can drive the app:

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

### What the agent does with it

With your app running, it can:

- **Explore** — dump the QML tree to learn the UI, then click and type its way through it
- **Generate tests** — record the exploration and export it as a Python test:

  ```bash
  uv run chuck mcp --port 1408 --record session.jsonl
  uv run chuck export session.jsonl --to-python test_flow.py
  ```

- **Repair** — replay a failing session and read the report to find renamed or moved items:

  ```bash
  uv run chuck repair session.jsonl --report report.json
  ```

See [docs/chuck/mcp.md](docs/chuck/mcp.md) and [docs/guide/ai-repair.md](docs/guide/ai-repair.md) for the full workflows, and `examples/text_editor/` for a skeleton app meant to be built this way.

## Building

Requires Qt 6, CMake, C++23.

```bash
cmake --preset <your-preset>
cmake --build --preset <your-build-preset>
```

## Documentation

```bash
uv run --group docs mkdocs serve
```

## Examples

- `examples/minimal/` — hello world
- `examples/todoapp/` — full app with StackView navigation, Material Design, and a test
- `examples/text_editor/` — barebone skeleton for AI-driven development (see its README for the prompt)

## License

chuck_tester is dual-licensed:

- **[AGPL-3.0-only](docs/legal/agpl-3.0-only.txt)** for open-source use
- **[Commercial license](docs/legal/LicenseRef-EliasSteurerTachiom.txt)** for closed-source / commercial use

See [docs/legal/](docs/legal/) for the full terms, CLAs, and contact details.
