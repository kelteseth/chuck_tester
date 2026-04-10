# Chuck + Testa — Open-Source Qt/QML AI-Driven Testing Framework

## Vision

An open-source (AGPLv3) two-part framework for testing Qt/QML desktop applications:

- **Chuck** — A C++ library embedded in the application under test (AUT). It exposes the QML object tree over the network and executes commands (click, type, swipe, screenshot, etc.).
- **Testa** — A Python (uv-managed) controller that drives the AUT. It can be used by an AI agent, by a human writing test scripts, or headlessly replaying recorded sessions.

The key innovation: an AI creates the test workflow interactively, the session is recorded as a deterministic replay file, and if a UI change later breaks that replay, the AI is invoked again to repair it.

---

## 1. Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                         testa (Python)                       │
│                                                              │
│  ┌──────────┐  ┌────────────┐  ┌──────────────────────────┐ │
│  │ AI Agent │  │ Replay     │  │ Visual Diff / Screenshot │ │
│  │ (MCP)    │  │ Engine     │  │ Comparison               │ │
│  └────┬─────┘  └─────┬──────┘  └───────────┬──────────────┘ │
│       │              │                      │                │
│       └──────────────┴──────────────────────┘                │
│                      │                                       │
│              ┌───────┴───────┐                               │
│              │  testa.client │  (Python WebSocket client)    │
│              └───────┬───────┘                               │
└──────────────────────┼───────────────────────────────────────┘
                       │  WebSocket (JSON-RPC 2.0 messages)
                       │
┌──────────────────────┼───────────────────────────────────────┐
│                      │              AUT Process               │
│              ┌───────┴───────┐                               │
│              │  chuck server │  (C++ WebSocket server)       │
│              └───────┬───────┘                               │
│                      │                                       │
│  ┌───────────────────┴──────────────────────────────────┐    │
│  │              chuck core                               │    │
│  │  ┌────────────┐ ┌──────────┐ ┌────────────────────┐  │    │
│  │  │ ItemFinder │ │ Commands │ │ ScreenshotProvider │  │    │
│  │  └────────────┘ └──────────┘ └────────────────────┘  │    │
│  └───────────────────────────────────────────────────────┘    │
│                                                              │
│  QQuickWindow (QML scene graph)                              │
└──────────────────────────────────────────────────────────────┘
```

---

## 2. Transport Decision: WebSockets

### Alternatives considered

| Transport | Pros | Cons |
|---|---|---|
| **QLocalSocket** | Fast, no port conflicts | Cannot test across machines — kills remote CI/cross-PC testing |
| **Raw TCP + length-prefix** | Minimal deps, full control | Must implement framing yourself, blocked by corporate firewalls/proxies, no browser tooling |
| **WebSockets** | Built-in message framing, passes HTTP proxies/firewalls, 3-line Python client (`websockets` lib), browser debug dashboard possible, already proven in the original code | `Qt::WebSockets` dependency (small) |
| **gRPC** | Typed schemas, streaming, huge ecosystem | Massive dependency (protoc, generated code), overkill for JSON-sized messages |
| **HTTP/REST** | Universal tooling | Request/response only (no server push without SSE), high overhead per call |

### Chosen: WebSockets

WebSockets are the right choice for this use case:

- **Free message framing** — no need to implement length-prefixed protocols. WebSocket handles message boundaries natively.
- **Firewall/proxy friendly** — WebSocket upgrades over HTTP, passing through corporate proxies and restrictive networks. Raw TCP often gets blocked.
- **Remote testing works out of the box** — the stated goal is testing across machines (CI runners, other PCs). WebSocket is designed for this.
- **Trivial clients in any language** — Python: `websockets` lib (3 lines). Browser: native `WebSocket` API. Node.js: `ws`. No custom framing code needed anywhere.
- **Browser debug dashboard** — a future web UI can connect directly to chuck from a browser. Raw TCP cannot do this.
- **Already working** — the original code proves it works. Rewriting to raw TCP adds risk for marginal benefit.
- **`Qt::WebSockets` is tiny** — the "extra dependency" argument is weak. It's a small module with no transitive deps.

### Binding
- Default port: `9223` (same as original)
- Bind to `127.0.0.1` by default (safe), with `--chuck-host 0.0.0.0` to allow remote
- Activated by CLI flag: `./myapp --chuck-port 9223`

---

## 3. Protocol: JSON-RPC 2.0

Instead of inventing a custom JSON protocol, we adopt [JSON-RPC 2.0](https://www.jsonrpc.org/specification) — it's a well-known standard with request/response/notification semantics that maps cleanly to what we need.

### Why JSON-RPC 2.0?
- Standardized `id`, `method`, `params`, `result`, `error` fields
- Supports notifications (no `id` → no response expected) — useful for events/screenshots
- Trivially parseable in both C++ (`QJsonDocument`) and Python (`json`)
- Maps directly to MCP tool calls (see §7)
- Well-understood by AI models (they already know the spec)

### Request examples

**Click an item:**
```json
{"jsonrpc": "2.0", "id": 1, "method": "click", "params": {"item": "createProjectBtn"}}
```

**Response:**
```json
{"jsonrpc": "2.0", "id": 1, "result": {"clicked": "createProjectBtn", "point": [120, 340]}}
```

**Swipe/scroll:**
```json
{"jsonrpc": "2.0", "id": 2, "method": "swipe", "params": {"item": "itemScrollView", "direction": "up", "distance": 300}}
```

**Type text:**
```json
{"jsonrpc": "2.0", "id": 3, "method": "keys", "params": {"item": "nameTextField", "text": "Hello World"}}
```

**Take screenshot:**
```json
{"jsonrpc": "2.0", "id": 4, "method": "screenshot", "params": {"item": "mainPage", "format": "png"}}
```
```json
{"jsonrpc": "2.0", "id": 4, "result": {"width": 1920, "height": 1080, "format": "png", "data": "<base64>"}}
```

**Wait for item:**
```json
{"jsonrpc": "2.0", "id": 5, "method": "wait", "params": {"item": "loadingSpinner", "property": "visible", "value": false, "timeout": 5000}}
```

**Find items (tree inspection):**
```json
{"jsonrpc": "2.0", "id": 6, "method": "find", "params": {"interactive": true, "accessible": true}}
```

### Full method table

| Method | Description | Key Params |
|---|---|---|
| `find` | Search QML tree for items | `type`, `interactive`, `accessible`, `depth`, `root`, `props` |
| `get` | Read properties of a specific item | `item`, `props` |
| `click` | Click/double-click/long-press an item | `item`, `button`, `double`, `hold` |
| `drag` | Drag from one item to another | `from`, `to` |
| `swipe` | Swipe/scroll gesture on an item | `item`, `direction`, `distance`, `duration` |
| `touch` | Multi-touch gesture | `item`, `gesture`, `points` |
| `keys` | Type text, send key, or keyboard shortcut | `item`, `text`, `key`, `shortcut` |
| `wait` | Wait for item/property (async) | `item`, `property`, `value`, `timeout` |
| `screenshot` | Capture screenshot | `item` (optional, entire window if omitted), `format` |
| `tree` | Dump full/partial QML tree as JSON | `root`, `depth`, `props` |
| `quit` | Gracefully close the AUT | — |

### Error format (JSON-RPC 2.0 standard)
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "error": {
    "code": -32001,
    "message": "Item not found: loginButton",
    "data": {"searchedTrees": ["content", "overlay"]}
  }
}
```

### Custom error codes
| Code | Meaning |
|---|---|
| -32001 | `item_not_found` |
| -32002 | `item_not_visible` |
| -32003 | `item_disabled` |
| -32004 | `item_blocked_by_modal` |
| -32005 | `ambiguous_item` |
| -32006 | `timeout` |
| -32007 | `property_not_found` |
| -32008 | `click_failed` |
| -32009 | `screenshot_failed` |

---

## 4. Object Identification — objectName Primary, Property Query Fallback

### Alternatives considered

| Strategy | Example | Resilience | Limitation |
|---|---|---|---|
| objectName only | `"saveBtn"` | Good if set, useless if not | Requires every testable item to have an objectName |
| Property dictionary (Squish-style) | `{type: "Button", text: "Save"}` | Survives tree restructuring | Verbose, text-based IDs break with i18n |
| Accessibility ID | `Accessible.name: "Save Project"` | Cross-platform standard | Not all items have Accessible props set |
| CSS-selector style | `Button[text="Save"]` | Familiar syntax | Custom parser needed, no Qt standard |
| XPath-like | `//Page[@name='settings']/Button[@text='Save']` | Very precise | Verbose, fragile to tree depth changes |
| Coordinate-based | `click(120, 340)` | Works for anything | Breaks with any layout/resolution change |

### Chosen: Hybrid — objectName + property query + path

The `item` parameter in every command accepts three forms:

**1. String — objectName lookup (primary, simplest)**
```json
{"item": "createProjectBtn"}
```
Fast BFS search by `objectName`. This is the happy path — developers should set objectNames on testable items.

**2. Object — property query (fallback for items without objectNames)**
```json
{"item": {"type": "Button", "text": "Save"}}
{"item": {"accessible_name": "Save Project"}}
{"item": {"type": "MenuItem", "text": "QML Application"}}
{"item": {"type": "TextField", "placeholderText": "Enter name..."}}
```
When an item doesn't have an objectName, the AI (or human) can query by any combination of properties. This matches how Squish's "Real Names" work — a property dictionary used for lookup.

Property queries are resolved by:
1. Walking the tree (same BFS as objectName)
2. Matching all specified properties against each item
3. Returning error if 0 or >1 items match (must be unambiguous)

**3. String with path syntax — disambiguation**

| Syntax | Meaning | Example |
|---|---|---|
| `name` | BFS by objectName (both trees) | `"createProjectBtn"` |
| `parent/child` | Path: find parent, then descend | `"settingsPage/themeCombo"` |
| `list[N]` | Delegate by index in a view's contentItem | `"projectList[0]"` |
| `parent/list[N]` | Combined path + delegate | `"mainPage/projectList[2]"` |
| `overlay/name` | Explicitly search popup/overlay tree | `"overlay/confirmDialog"` |

### Resolution order

When `item` is a string:
1. Try `objectName` BFS (both content and overlay trees)
2. If not found, try QML type name match
3. If ambiguous, return error with disambiguation hints

When `item` is an object (property query):
1. Walk tree, match all specified properties
2. Error if 0 matches (`item_not_found`) or >1 matches (`ambiguous_item`)
3. Response includes the matched item's objectName/path for future reference

### Why this hybrid approach?
- **objectName is ideal** when available — fast, simple, stable, readable
- **Property queries handle the real world** — not every item has an objectName, especially third-party components, dynamic delegates, or deeply nested controls
- **The AI adapts** — when `find` returns the tree, the AI picks the best identifier (objectName if available, property query if not)
- **Squish-proven** — Squish's most robust mode is property-based identification, not name-only

### Convention for AUT developers
```qml
Button {
    objectName: "createProjectBtn"  // ← set this for testability
    text: "Create Project"
}
```
Items without objectNames are still testable via property queries — but objectNames make tests more readable and faster to resolve.

---

## 5. Recording Format: Dual-Format (JSONL + Python Scripts)

When an AI (or human) interacts with the app through testa, every action is recorded. The system uses two complementary formats: **JSONL** as the raw recording format and **Python scripts** as the human-facing test format.

### Alternatives considered

| Format | Pros | Cons |
|---|---|---|
| **TOML** | Human-readable, comments, hand-editable | Verbose for 100+ step sequences, `tomli-w` is third-party, can't embed logic/variables, no streaming append |
| **YAML** | Compact sequences, comments, widely known | Whitespace footguns, "Norway problem" (`NO` → `false`), complex spec |
| **JSON** | Universal parsing, schema validation | No comments, verbose, not hand-friendly |
| **JSONL** (JSON Lines) | One JSON object per line, dead simple to stream/append during recording, trivially parseable, grep-friendly | No comments, not as pretty to hand-edit |
| **Python scripts** | The test IS the recording. Full language power (loops, assertions, conditionals). Pytest integration for free. AI generates Python natively. Playwright/Puppeteer proved this works at scale. | Not pure "data" — harder to parse/transform mechanically |
| **SQLite** | Binary screenshots as BLOBs (no base64!), queryable, transactional, single file | Not hand-editable, opaque format |
| **Gherkin/BDD** | Very readable (`When I click "saveBtn"`), established testing pattern | Needs mapping layer, limited expressiveness |

### Chosen: JSONL for recording, Python for tests

**JSONL is the raw recording format** — dead simple, streamable, one action per line:

```jsonl
{"ts": "2026-04-10T14:32:00.000Z", "action": "wait", "item": "mainWindow", "property": "visible", "value": true, "timeout": 10000}
{"ts": "2026-04-10T14:32:01.234Z", "action": "screenshot", "tag": "initial_state"}
{"ts": "2026-04-10T14:32:01.500Z", "action": "click", "item": "createProjectBtn"}
{"ts": "2026-04-10T14:32:02.100Z", "action": "wait", "item": "projectNameField", "property": "visible", "value": true}
{"ts": "2026-04-10T14:32:02.300Z", "action": "keys", "item": "projectNameField", "text": "My Test Project"}
{"ts": "2026-04-10T14:32:03.000Z", "action": "click", "item": "projectTypeCombo"}
{"ts": "2026-04-10T14:32:03.500Z", "action": "click", "item": {"type": "MenuItem", "text": "QML Application"}}
{"ts": "2026-04-10T14:32:04.000Z", "action": "screenshot", "tag": "project_configured"}
{"ts": "2026-04-10T14:32:04.200Z", "action": "click", "item": "confirmBtn"}
{"ts": "2026-04-10T14:32:05.500Z", "action": "wait", "item": "projectList[0]", "timeout": 5000}
{"ts": "2026-04-10T14:32:06.000Z", "action": "screenshot", "tag": "project_created", "verify": true}
```

Why JSONL:
- **Streaming append** — each action is written as it happens, no need to buffer/serialize a whole document
- **Trivial parsing** — `for line in file: json.loads(line)` in Python, one line of code
- **Grep-friendly** — `grep '"click"' session.jsonl` finds all clicks
- **Convertible** — trivially transforms to/from Python scripts, JSON, or any other format
- **Timestamps** — `ts` field records real timing for optional timing-aware replay

**Python scripts are the test format** — Playwright proved this model works at massive scale:

```python
# test_create_project.py — generated by: testa export session.jsonl
import pytest
from testa import TestaClient

@pytest.mark.asyncio
async def test_create_project():
    async with TestaClient(port=9223) as app:
        await app.wait("mainWindow", property="visible", value=True)
        await app.screenshot("initial_state")

        # Create a new project
        await app.click("createProjectBtn")
        await app.wait("projectNameField", property="visible", value=True)
        await app.keys("projectNameField", text="My Test Project")

        # Select project type from dropdown
        await app.click("projectTypeCombo")
        await app.click({"type": "MenuItem", "text": "QML Application"})
        await app.screenshot("project_configured")

        # Confirm and verify
        await app.click("confirmBtn")
        await app.wait("projectList[0]", timeout=5000)
        await app.screenshot("project_created", verify=True)
```

Why Python scripts:
- **Full language power** — `if`, `for`, assertions, variables, data-driven tests
- **AI-native** — AI models generate and repair Python fluently
- **Pytest integration** — free test runner, fixtures, reporting, parallelization
- **Self-documenting** — comments explain intent, not just actions
- **Playwright precedent** — proven model adopted by millions of developers

### Conversion between formats

```bash
# Record → JSONL (automatic during AI/MCP sessions)
uv run testa record --output session.jsonl

# JSONL → Python test script
uv run testa export session.jsonl --to-python test_flow.py

# Python test → JSONL (re-record)
uv run testa record --script test_flow.py --output updated.jsonl

# Replay from either format
uv run testa replay session.jsonl
uv run testa replay test_flow.py  # just runs pytest
```

### Step schema (JSONL)

Every line has:
- `action` — required, one of the method names from §3
- `item` — string (objectName) or object (property query, see §4)
- `ts` — ISO 8601 timestamp (recording time, optional for hand-written)
- Additional params matching the JSON-RPC method params
- `tag` — optional label for screenshots and logging
- `verify` — optional boolean, if `true` the screenshot is compared against a baseline

### Verification screenshots
When `verify` is true, testa during replay:
1. Takes a screenshot at that step
2. Compares against `baselines/<tag>.png` using configured strategy (SSIM/pHash/exact)
3. On mismatch: saves a diff image and marks the step as failed
4. The AI repair flow can then inspect the diff and decide what changed

---

## 6. Testa — The Python Controller

### Why Python (not C++)?
- **Faster iteration**: no compilation, instant feedback
- **AI-native**: AI models generate Python fluently; Python is the lingua franca of AI tooling
- **Rich ecosystem**: `Pillow` for image comparison, `asyncio` for async I/O, `pytest` for test integration
- **uv-managed**: single `uv run testa` to install deps and run — no virtualenv hassle
- **Lower barrier**: contributors don't need a C++ toolchain to write tests

### Project structure

```
testa/
├── pyproject.toml          # uv project config
├── testa/
│   ├── __init__.py
│   ├── __main__.py         # CLI entry point
│   ├── client.py           # WebSocket client, JSON-RPC 2.0
│   ├── commands.py         # High-level API: click(), keys(), screenshot(), etc.
│   ├── recorder.py         # Records actions to JSONL files
│   ├── replayer.py         # Reads JSONL and replays step-by-step
│   ├── exporter.py         # Converts JSONL ↔ Python test scripts
│   ├── repair.py           # AI-driven repair when replay fails
│   ├── screenshots.py      # Screenshot capture, baseline management, diff
│   ├── visual_diff.py      # Pixel comparison, perceptual hashing, diff images
│   ├── mcp_server.py       # MCP tool server (exposes chuck commands as MCP tools)
│   └── wait.py             # Smart waiting utilities
└── tests/
    └── ...
```

### CLI interface

```bash
# Replay a recorded session (JSONL)
uv run testa replay session.jsonl --aut-port 9223

# Replay a Python test script (runs via pytest)
uv run testa replay test_create_project.py --aut-port 9223

# Record a new session (AI-driven)
uv run testa record --aut-port 9223 --output session.jsonl

# Export JSONL to Python test script
uv run testa export session.jsonl --to-python test_create_project.py

# Start as MCP server (for Claude/other AI)
uv run testa mcp --aut-port 9223

# Interactive REPL for manual testing
uv run testa repl --aut-port 9223

# Update baselines from current screenshots
uv run testa baseline session.jsonl --aut-port 9223

# Compare screenshots against baselines (CI mode)
uv run testa verify session.jsonl --aut-port 9223 --fail-on-diff
```

### High-level Python API

```python
from testa import TestaClient

async with TestaClient(host="127.0.0.1", port=9223) as app:
    # Wait for app to load
    await app.wait("mainWindow", property="visible", value=True)
    
    # Take a screenshot
    img = await app.screenshot()
    
    # Click a button
    await app.click("createProjectBtn")
    
    # Type into a text field (by objectName)
    await app.keys("projectNameField", text="My Project")
    
    # Click a dropdown item (by property query — no objectName needed)
    await app.click({"type": "MenuItem", "text": "QML Application"})
    
    # Swipe a scroll view
    await app.swipe("itemScrollView", direction="up", distance=300)
    
    # Inspect the QML tree
    items = await app.find(interactive=True, accessible=True)
    
    # Get properties of a specific item
    props = await app.get("projectNameField", props="text,enabled")
    
    # Screenshot with visual comparison
    result = await app.screenshot(tag="final_state", verify=True)
    assert result.match  # True if matches baseline within tolerance
```

---

## 7. MCP Integration — Testa as an MCP Server

Testa can run as an [MCP (Model Context Protocol)](https://modelcontextprotocol.io/) tool server, exposing chuck commands as MCP tools. This lets any MCP-compatible AI (Claude, etc.) drive the AUT directly.

### How it works
```
AI Agent ←→ MCP Protocol ←→ testa (mcp_server.py) ←→ WebSocket ←→ chuck (in AUT)
```

### MCP tools exposed

| MCP Tool | Maps to |
|---|---|
| `chuck_find` | `find` — discover interactive items |
| `chuck_get` | `get` — read item properties |
| `chuck_click` | `click` — click an item |
| `chuck_keys` | `keys` — type/shortcut/special keys |
| `chuck_swipe` | `swipe` — scroll gesture |
| `chuck_screenshot` | `screenshot` — capture screen (returns image) |
| `chuck_wait` | `wait` — wait for condition |
| `chuck_tree` | `tree` — dump QML tree |

### Recording during MCP sessions
When testa runs as an MCP server with `--record session.jsonl`, every tool call is automatically recorded as JSONL. The AI doesn't need to know about recording — it just interacts normally, and testa captures everything.

### The repair loop
```
1. CI runs: `uv run testa replay test_create_project.py` (or session.jsonl)
2. Step N fails (item not found, visual mismatch, etc.)
3. CI runs: `uv run testa repair test_create_project.py --step N`
4. testa starts the AUT, replays steps 1..N-1, then invokes the AI
5. AI sees: current screenshot, the failed step, the error, the diff image
6. AI fixes the step (e.g. objectName changed, property query updated) or adds new steps
7. Updated test script / JSONL is saved
8. CI re-runs the full replay to verify the fix
```

---

## 8. Chuck — The C++ Embedded Library

### Namespace
```cpp
namespace chuck { ... }
```

### Public API (single header: `chuck/ChuckServer.h`)

```cpp
namespace chuck {

struct Config {
    quint16 port = 9223;
    QHostAddress host = QHostAddress::LocalHost;
};

class ChuckServer : public QObject {
    Q_OBJECT
public:
    explicit ChuckServer(QQuickWindow& window, Config config = {});
    ~ChuckServer();
    
    bool start();
    void stop();
    
signals:
    void clientConnected();
    void clientDisconnected();
    void commandReceived(const QString& method);
};

} // namespace chuck
```

### Usage in the AUT

```cpp
#include <chuck/ChuckServer.h>

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    engine.load(QUrl("qrc:/main.qml"));
    
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    
    // Only start if requested (e.g., --chuck-port 9223)
    chuck::ChuckServer server(*window);
    server.start();
    
    return app.exec();
}
```

### CMake integration

```cmake
find_package(chuck REQUIRED)
target_link_libraries(myapp PRIVATE chuck::chuck)
```

Or as a subdirectory:
```cmake
add_subdirectory(chuck)
target_link_libraries(myapp PRIVATE chuck::chuck)
```

### Dependencies
- `Qt6::Core`
- `Qt6::Quick`
- `Qt6::Qml`
- `Qt6::WebSockets` (WebSocket server — proven in original code)
- `Qt6::Test` (for `QTest::mouseClick`, `QTest::keyClick`, etc.)

### Internal structure

```
chuck/
├── CMakeLists.txt
├── include/
│   └── chuck/
│       └── ChuckServer.h          # Public API (only public header)
├── src/
│   ├── ChuckServer.cpp            # WebSocket server, client management
│   ├── CommandHandler.h/.cpp      # JSON-RPC dispatch → command methods
│   ├── ItemFinder.h/.cpp          # QML tree walking, identifier + property query parsing
│   ├── PendingWait.h/.cpp         # Async wait with timeout
│   ├── ScreenshotProvider.h/.cpp  # Grab window/item, encode PNG/JPEG
│   └── TouchSimulator.h/.cpp      # Touch/swipe/pinch gesture synthesis
└── tests/
    └── ...
```

---

## 9. Helper Toolbox

Common test helpers are built into chuck's command set and testa's Python API as protocol-level commands:

| Operation | chuck command | testa Python |
|---|---|---|
| Find delegate by index | `find` with `list[N]` syntax | `app.find("list[N]")` |
| Click item | `click` | `app.click("btn")` |
| Wait for visibility | `wait` with `visible`/`true` | `app.wait("item", property="visible", value=True)` |
| Wait for condition | `wait` with property/value | `app.wait("item", property="prop", value=val)` |
| Find child recursively | `find` with path syntax | `app.find("parent/child")` |

### Additional interaction commands (new)

| Category | Command | Description |
|---|---|---|
| **Mouse** | `click` | Single/double/right/middle click |
| | `drag` | Drag from item A to item B |
| | `hover` | Move mouse over item (triggers hover effects) |
| | `long_press` | Press and hold (context menu, etc.) |
| **Keyboard** | `keys` | Type text, special keys, shortcuts |
| | `focus` | Force focus to an item |
| **Touch** | `swipe` | Swipe gesture on an item |
| | `pinch` | Pinch-to-zoom gesture |
| | `tap` | Touch tap (distinct from mouse click) |
| | `multi_touch` | Arbitrary multi-point touch sequence |
| **Screenshot** | `screenshot` | Full window or specific item |
| | `screenshot_diff` | Compare against baseline (done in testa) |
| **Tree** | `find` | Search for items by filters |
| | `get` | Read item properties |
| | `tree` | Dump tree structure |
| **Flow** | `wait` | Wait for item/property/condition |
| | `quit` | Close the AUT |

---

## 10. Screenshot & Visual Comparison System

### Capture (chuck-side)

```cpp
// ScreenshotProvider grabs from the QQuickWindow
QImage image = window->grabWindow();
// Or grab a specific item:
QSharedPointer<QQuickItemGrabResult> result = item->grabToImage();
```

The screenshot is PNG-encoded and sent as base64 in the JSON-RPC response.

### Comparison (testa-side, Python)

```
testa/
├── baselines/           # Checked-in reference screenshots
│   ├── initial_state.png
│   ├── project_created.png
│   └── ...
├── screenshots/         # Latest captures (gitignored)
│   └── ...
└── diffs/               # Visual diff images (gitignored)
    └── ...
```

### Comparison strategies

1. **Pixel-exact** — strict, for pixel-perfect UIs
2. **Perceptual hash** (pHash) — tolerant of anti-aliasing, sub-pixel rendering differences
3. **Structural similarity** (SSIM) — from `scikit-image`, good for "is it roughly the same?"
4. **Region masking** — ignore dynamic areas (timestamps, random IDs) via mask files
5. **AI-assisted** — when structural comparison fails, send both images to an AI for semantic comparison ("does this still show a login form?")

### Configuration in session or pyproject.toml

```toml
# In pyproject.toml [tool.testa] or a testa.toml config file
[tool.testa.visual]
strategy = "ssim"           # "exact", "phash", "ssim"
threshold = 0.95            # 0.0 - 1.0 similarity required
mask = "masks/ignore_timestamp.png"  # optional mask
```

---

## 11. Comparison with Squish

| Aspect | Squish | Chuck + Testa |
|---|---|---|
| **License** | Commercial ($$$) | AGPLv3 (open source) |
| **Architecture** | Hook injected into AUT + squishserver + squishrunner | Chuck lib linked by AUT + testa controller |
| **Object ID** | Real Names (property dict) + Symbolic Names (aliases) | objectName primary + property query fallback (hybrid) |
| **Transport** | Custom TCP protocol | JSON-RPC 2.0 over WebSocket |
| **Script language** | Python, JavaScript, Perl, Ruby, Tcl | Python (testa), or any language via WebSocket |
| **Recording** | Squish IDE recorder (GUI) | AI-driven recording → JSONL + Python test export |
| **Visual testing** | Screenshot VP in XML | PNG baselines + SSIM/pHash comparison |
| **AI integration** | Recent "AI code assistants" add-on | First-class: MCP server, AI repair loop |
| **Replay** | squishrunner executes scripts | `testa replay session.jsonl` or `pytest test_flow.py` |
| **Test repair** | Manual by developer | AI-driven automatic repair |

The main philosophical difference: Squish is a traditional record-and-playback tool with AI bolted on. Chuck+Testa is AI-first: the AI creates the tests, and deterministic replay (JSONL / Python scripts) is the artifact.

---

## 12. File & Directory Layout

```
chuck/                          # Repository root
├── CMakeLists.txt              # Top-level CMake (builds chuck lib + examples)
├── CMakePresets.json
├── PLAN.md                     # This file
├── LICENSE                     # AGPLv3
├── README.md
│
├── chuck/                      # C++ library (embedded in AUT)
│   ├── CMakeLists.txt
│   ├── include/
│   │   └── chuck/
│   │       └── ChuckServer.h   # Only public header
│   └── src/
│       ├── ChuckServer.cpp
│       ├── CommandHandler.h
│       ├── CommandHandler.cpp
│       ├── ItemFinder.h
│       ├── ItemFinder.cpp
│       ├── PendingWait.h
│       ├── PendingWait.cpp
│       ├── ScreenshotProvider.h
│       ├── ScreenshotProvider.cpp
│       ├── TouchSimulator.h
│       ├── TouchSimulator.cpp
│       └── Protocol.h/.cpp
│
├── testa/                      # Python controller
│   ├── pyproject.toml          # uv project
│   ├── testa/
│   │   ├── __init__.py
│   │   ├── __main__.py
│   │   ├── client.py
│   │   ├── commands.py
│   │   ├── recorder.py
│   │   ├── replayer.py
│   │   ├── exporter.py
│   │   ├── repair.py
│   │   ├── screenshots.py
│   │   ├── visual_diff.py
│   │   ├── mcp_server.py
│   │   └── wait.py
│   └── tests/
│
├── examples/                   # Example AUTs with chuck embedded
│   ├── minimal/
│   │   ├── CMakeLists.txt
│   │   ├── main.cpp
│   │   └── main.qml
│   └── sessions/
│       ├── example_session.jsonl
│       └── test_example.py
│
```

---

## 13. Implementation Phases

### Phase 1: Chuck Core (C++)
1. Port `AutomationServer` → `ChuckServer` (keep WebSocket, adapt to JSON-RPC 2.0)
2. Adapt `CommandHandler` to JSON-RPC 2.0 format
3. Port `ItemFinder` to `chuck` namespace, add property query resolution
4. Port `PendingWait` to `chuck` namespace
5. Add `ScreenshotProvider` (window grab + item grab + PNG encode + base64)
6. Add `swipe`/`touch` commands via `TouchSimulator`
7. Single public header `chuck/ChuckServer.h`
8. CMake: build as static/shared lib with proper install targets

### Phase 2: Testa Client (Python)
1. WebSocket client with JSON-RPC 2.0 (`client.py`)
2. High-level async API (`commands.py`)
3. CLI skeleton with `click`, `argparse` or `typer`
4. Interactive REPL mode
5. JSONL recording (`recorder.py`)
6. JSONL replay (`replayer.py`)
7. JSONL → Python export (`exporter.py`)

### Phase 3: Screenshots & Visual Diff
1. Screenshot command in chuck (full window + item-specific)
2. Screenshot capture and save in testa
3. Baseline management (save, update, organize)
4. SSIM / perceptual hash comparison
5. Diff image generation (highlight changed regions)
6. Region masking for dynamic content

### Phase 4: MCP Server
1. `mcp_server.py` — expose chuck tools as MCP tools
2. AI can drive the AUT through standard MCP
3. Automatic recording during MCP sessions
4. `--record` flag for MCP mode

### Phase 5: AI Repair Loop
1. Detect replay failures (step-level error reporting)
2. Collect context: error, screenshot, diff, surrounding steps
3. Invoke AI to propose fix (updated Python script or JSONL)
4. Apply fix and re-verify
5. CI integration (`testa repair` command)

### Phase 6: Polish & Ecosystem
1. Documentation (README, API docs, tutorials)
2. Example apps with pre-recorded sessions + generated Python tests
3. pytest plugin (`pytest-testa`) for test framework integration
4. GitHub Actions / CI templates
5. Optional browser debug dashboard (WebSocket connects natively from browsers)

---

## 14. Open Questions & Decisions Needed

| # | Question | Leaning | Notes |
|---|---|---|---|
| 1 | Static or shared lib for chuck? | Both (CMake option) | Static for simple embedding, shared for plugins |
| 2 | Minimum Qt version? | Qt 6.5+ | For `std::expected` support in C++23 and modern QML |
| 3 | Python minimum version? | 3.12+ | Modern asyncio, tomllib in stdlib |
| 4 | Should chuck support multiple simultaneous clients? | Yes | One recorder + one AI + one manual REPL |
| 5 | Should we support `QWidget` apps too, or only QML? | QML first | QWidget support is a future extension |
| 6 | Should testa also handle AUT lifecycle (start/stop)? | Yes | `testa run ./myapp --session test.jsonl` |
| 7 | Screenshot transport: base64 in JSON or secondary binary channel? | Base64 first | Simple; add binary channel later if perf matters |
| 8 | Touch events: use QTest or synthesize QTouchEvent directly? | QTouchEvent directly | QTest has limited touch support |
| 9 | Should the recording include timing between steps? | Yes, `ts` field | Recorded automatically, timing-aware replay is optional |
| 10 | When to use property query vs objectName? | objectName preferred, property query fallback | AI picks the best identifier from tree inspection |

---

## 15. Summary of Key Design Choices

| Decision | Choice | Rationale |
|---|---|---|
| Transport | WebSockets | Free framing, firewall-friendly, proven in original code, browser-compatible |
| Protocol | JSON-RPC 2.0 | Standard, AI-friendly, maps to MCP, same wire format as LSP |
| Object ID | objectName primary + property query fallback | objectName when available (simple, stable), property query for items without names (Squish-style resilience) |
| Recording format | JSONL (raw) + Python scripts (tests) | JSONL for streaming recording, Python for human-facing tests with full language power (Playwright model) |
| Controller language | Python (uv) | AI-native, fast iteration, rich ecosystem |
| Embedded library | C++ (Qt) | Must live in the AUT process |
| Visual comparison | SSIM + pHash + masking | Robust across platforms and themes |
| AI integration | MCP server | Standard protocol, works with any MCP client |
| Repair strategy | AI re-drives failed steps | Novel: tests self-heal via AI |
