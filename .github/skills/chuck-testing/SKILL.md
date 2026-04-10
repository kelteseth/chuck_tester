---
name: chuck-testing
description: 'Write integration tests for Qt/QML apps using the chuck + tester framework. Use when: creating test files, writing pytest async tests, driving QML UI with clicks/keys/waits, handling StackView navigation, waiting for animations, asserting properties, screenshot verification, debugging test failures.'
---

# Writing Tests with Chuck + Tester

## AI-Driven Development Loop

When building a Qt/QML app incrementally, use chuck as your live validation tool — not just a way to write test files for humans to run later. After each feature is implemented:

1. Build the target (`uv run build.py --preset <preset> --target <name>`)
2. Launch the exe (see cpp-build skill for Qt DLL PATH requirements)
3. Connect and assert inline — no pytest file needed at this stage:

```bash
uv run --directory chuck --extra dev python -c "
import asyncio
from chuck import ChuckClient

async def check():
    async with ChuckClient(port=1408) as app:
        await app.wait('myItem', property='visible', value=True, timeout=5000)
        props = await app.get('myLabel', props='text')
        assert props['text'] == 'expected', props
        print('PASS')
        await app.quit()

asyncio.run(check())
"
```

4. Fix and rebuild if any assertion fails; only move to the next feature once this passes.

Write the formal `test_*.py` / `test_suite.py` files at the end, after the feature set is complete and live-validated.

## Architecture

**tester** is a C++ static library linked into the Qt/QML app under test (AUT). It runs a WebSocket server (default port 1408) that accepts JSON-RPC 2.0 commands to find, click, type, wait, and screenshot QML items.

**chuck** is the Python controller. Tests use `ChuckClient` (async context manager) to send commands over WebSocket. Tests run with `pytest` + `pytest-asyncio`.

```
┌──────────────┐  WebSocket/JSON-RPC  ┌────────────────┐
│  chuck test   │ ◀─────────────────▶ │  tester server  │
│  (Python)     │      port 1408       │  (in AUT)       │
└──────────────┘                       └────────────────┘
```

## Prerequisites

The AUT must be running with tester enabled before tests execute. The test file does NOT launch the app — start it manually or via a separate script.

```cpp
// main.cpp — add tester to any Qt Quick app
#include <tester/TesterServer.h>

// After QQmlApplicationEngine loads:
auto *window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
tester::TesterServer server(*window);
server.start();
```

```cmake
# CMakeLists.txt — link against tester
target_link_libraries(myapp PRIVATE Qt6::Quick Qt6::Qml tester)
```

## Test File Structure

Every test follows this pattern:

```python
import pytest
from chuck import ChuckClient


@pytest.mark.asyncio
async def test_my_feature():
    async with ChuckClient(port=1408) as app:
        # Wait for the app to be ready
        await app.wait("mainItem", property="visible", value=True, timeout=5000)

        # Test steps...
        await app.click("myButton")
        props = await app.get("myLabel", props="text")
        assert props["text"] == "expected"

        await app.quit()
```

Key rules:
- One `async with ChuckClient` block per test — it connects/disconnects automatically
- Always `await app.wait(...)` for the initial item before interacting
- All commands are `await`-ed — they are async JSON-RPC calls
- End with `await app.quit()` to close the AUT gracefully
- No fixtures or subprocess management needed — the app runs independently

## Running Tests

```bash
# From the repo root (chuck is a subdirectory with its own pyproject.toml)
cd chuck
uv run --extra dev pytest ../examples/myapp/test_myapp.py -v

# Or from repo root:
uv run --directory chuck --extra dev pytest examples/myapp/test_myapp.py -v
```

## ChuckClient API Reference

### Item Identification

Items are found by `objectName`. Every QML item you want to interact with needs an `objectName`:

```qml
Button {
    objectName: "saveBtn"   // ← this is what the test uses
    text: "Save"
}
```

Items can also be found by property queries (dict):

```python
await app.click({"text": "Save", "type": "Button"})
```

### Commands

#### `click(item, *, button=None, double=False, hold=None, root=None)`
Click an item. The item must be visible, enabled, and not blocked by a modal.

```python
await app.click("saveBtn")
await app.click("item", button="right")        # right-click
await app.click("item", double=True)            # double-click
await app.click("item", hold=1000)              # long press (ms)
await app.click("childItem", root="parentItem") # scoped search
```

#### `keys(item=None, *, text=None, key=None, shortcut=None, root=None)`
Type text, send a special key, or trigger a keyboard shortcut.

```python
await app.keys("nameField", text="Hello world")       # type text into focused item
await app.keys("nameField", shortcut="Ctrl+A")         # select all
await app.keys("nameField", key="Backspace")            # special key
await app.keys(shortcut="Ctrl+S")                       # global shortcut (no item)
```

#### `get(item, *, props=None)`
Read properties from an item. Returns a dict.

```python
props = await app.get("myLabel", props="text")          # specific property
props = await app.get("myLabel", props="text,visible")   # multiple properties
props = await app.get("myLabel", props="custom")         # only user-defined properties
props = await app.get("myLabel")                         # all properties
```

#### `wait(item, *, property=None, value=None, timeout=5000)`
Wait for an item to appear in the tree, or for a property to reach a value. This is the primary synchronization primitive.

```python
await app.wait("myItem", timeout=3000)                              # wait for item to exist
await app.wait("myItem", property="visible", value=True, timeout=5000)  # wait for visibility
await app.wait("stack", property="busy", value=False, timeout=3000)     # wait for animation end
```

#### `find(*, type=None, interactive=False, accessible=False, depth=None, root=None, props=None)`
Search the QML tree for items matching criteria.

```python
items = await app.find(interactive=True)           # all clickable items
items = await app.find(type="Button")               # all Buttons
items = await app.find(root="myPage", depth=3)      # scoped + depth limit
```

#### `tree(*, root=None, depth=None, props=None)`
Dump the full QML item tree. Invaluable for debugging.

```python
items = await app.tree(depth=4)
for item in items:
    print(item["path"], item["objectName"], item["type"])
```

#### `drag(from_item, to_item, *, root=None)`
Drag from one item to another.

```python
await app.drag("sourceItem", "targetItem")
```

#### `swipe(item, *, direction="up", distance=200)`
Swipe gesture. Directions: `"up"`, `"down"`, `"left"`, `"right"`.

```python
await app.swipe("listView", direction="up", distance=300)
```

#### `screenshot(item=None, *, tag=None, verify=False, save_to=None, method="ssim", threshold=0.95)`
Capture a screenshot. Can save to file and/or verify against a stored baseline.

```python
await app.screenshot(save_to="screenshots/step1.png")           # save to file
await app.screenshot("myWidget", save_to="screenshots/widget.png")  # crop to item
await app.screenshot(tag="login_page", verify=True)              # baseline comparison
```

#### `quit()`
Gracefully close the application.

```python
await app.quit()
```

## Synchronization Patterns

### Waiting for App Startup

Always wait for a known item to become visible before interacting:

```python
await app.wait("listPage", property="visible", value=True, timeout=5000)
```

### StackView Navigation

When using `StackView`, wait for the `busy` property to become `false` after every push/pop. The `busy` property is `true` during transition animations.

```python
STACK = "stack"  # objectName of the StackView

async def wait_nav(app: ChuckClient):
    """Wait for StackView transition animation to finish."""
    await app.wait(STACK, property="busy", value=False, timeout=3000)

# Usage:
await app.click("addBtn")          # triggers stack.push()
await wait_nav(app)                 # wait for push animation
await app.keys("titleField", text="Hello")
await app.click("saveBtn")         # triggers stack.pop()
await wait_nav(app)                 # wait for pop animation
```

**Never disable animations to work around timing.** Use property waits instead — tests must work with real apps.

### Model/Delegate Changes

When a model changes (add/remove rows), ListView delegates are recreated. Wait for the new delegate to become visible before clicking:

```python
await app.click("deleteBtn_1")                                          # removes item from model
await app.wait("deleteBtn_0", property="visible", value=True, timeout=3000)  # wait for re-layout
await app.click("deleteBtn_0")
```

### Waiting for Item to Appear

After a model insertion, the delegate item may take a frame to appear:

```python
await app.click("saveBtn")
await wait_nav(app)                                # wait for StackView pop
await app.wait("todoItem_0", timeout=3000)          # wait for delegate to exist
```

## Error Codes

Tester returns these JSON-RPC error codes (raised as `ChuckError` in Python):

| Code | Name | Meaning |
|------|------|---------|
| -32001 | ItemNotFound | No item with that objectName/query exists |
| -32002 | ItemNotVisible | Item exists but `isVisible()` is false or off-screen |
| -32003 | ItemDisabled | Item exists but `isEnabled()` is false |
| -32004 | ItemBlockedByModal | A modal popup is covering the item |
| -32005 | AmbiguousItem | Multiple items match the query |
| -32006 | Timeout | `wait()` timed out |
| -32007 | PropertyNotFound | Requested property doesn't exist on the item |
| -32008 | ClickFailed | Could not compute click coordinates |
| -32009 | ScreenshotFailed | Screenshot capture failed |

Handle errors:

```python
from chuck import ChuckError

try:
    await app.click("maybeBtn")
except ChuckError as e:
    if e.code == -32001:
        print("Button not in tree yet")
```

## QML Side: objectName Best Practices

### Static Names
```qml
Button { objectName: "saveBtn" }
TextField { objectName: "titleField" }
Page { objectName: "listPage" }
StackView { objectName: "stack" }
```

### Dynamic Names in Delegates
Use the delegate's `index` to create unique names:

```qml
ListView {
    delegate: ItemDelegate {
        id: del
        objectName: "todoItem_" + index
        required property int index

        contentItem: RowLayout {
            CheckBox { objectName: "check_" + del.index }
            Label { objectName: "label_" + del.index }
            ToolButton { objectName: "editBtn_" + del.index }
            ToolButton { objectName: "deleteBtn_" + del.index }
        }
    }
}
```

**Important:** Reference the delegate's `index` via its `id` (e.g., `del.index`), not `model.index` or the bare `index` property, to avoid binding issues in nested items.

## Debugging Tests

### Dump the Item Tree

When an item isn't found, dump the tree to see what's actually there:

```python
items = await app.tree(depth=5)
for item in items:
    name = item.get("objectName", "")
    if name:
        print(f"  {item['path']}  objectName={name}  type={item['type']}")
```

### Check Item Visibility

```python
props = await app.get("myItem", props="visible,enabled,width,height,x,y")
print(props)
```

### Common Failure Causes

1. **"Item not found"** — The objectName is wrong, or the item hasn't been created yet. Use `wait()` before interacting.

2. **"Item not visible"** — The item exists but is hidden (e.g., on a different StackView page, or a delegate that was removed). Wait for `visible=True` or for the navigation animation to finish.

3. **"Item blocked by modal"** — A Dialog or Popup is open. Close it first.

4. **"Timeout waiting for property"** — The condition never became true. Check that the property name and expected value are correct. Use `get()` to read the current value.

5. **StackView.view.pop() returns null** — The `StackView.view` attached property only resolves on the item directly pushed to the StackView. Nested children must qualify: `editPage.StackView.view.pop()` not `StackView.view.pop()`.

6. **Header/footer items not found** — Tester traverses Page `header` and `footer` properties. Items inside headers/footers (ToolBar, etc.) are discoverable by objectName.

## Complete Example

```python
# examples/todoapp/test_todoapp.py
import pytest
from chuck import ChuckClient

STACK = "stack"


async def wait_nav(app: ChuckClient):
    """Wait for StackView transition animation to finish."""
    await app.wait(STACK, property="busy", value=False, timeout=3000)


@pytest.mark.asyncio
async def test_todoapp():
    async with ChuckClient(port=1408) as app:
        await app.wait("listPage", property="visible", value=True, timeout=5000)

        # Verify app started
        props = await app.get("listPage", props="visible")
        assert props["visible"] is True

        # Add a todo
        await app.click("addBtn")
        await wait_nav(app)
        await app.keys("titleField", text="Buy milk")
        await app.click("saveBtn")
        await wait_nav(app)
        await app.wait("todoItem_0", timeout=3000)

        # Toggle checkbox
        await app.click("check_0")
        props = await app.get("check_0", props="checked")
        assert props["checked"] is True

        # Edit the todo
        await app.click("editBtn_0")
        await wait_nav(app)
        await app.keys("titleField", shortcut="Ctrl+A")
        await app.keys("titleField", text="Buy oat milk")
        await app.click("saveBtn")
        await wait_nav(app)
        props = await app.get("label_0", props="text")
        assert props["text"] == "Buy oat milk"

        # Delete
        await app.wait("deleteBtn_0", property="visible", value=True, timeout=3000)
        await app.click("deleteBtn_0")

        # Save/Load cycle
        await app.keys(shortcut="Ctrl+S")
        await app.keys(shortcut="Ctrl+L")

        await app.quit()
```

## Screenshot Baseline Testing

```python
# First run creates baselines; subsequent runs compare against them
await app.screenshot(tag="main_page", verify=True, threshold=0.95)

# Methods: "ssim" (default), "phash", "exact"
await app.screenshot(tag="dialog", verify=True, method="phash", threshold=0.9)

# Mask dynamic regions (x, y, width, height)
await app.screenshot(tag="page", verify=True, mask_regions=[(10, 200, 300, 50)])
```

Baselines are stored in a `baselines/` directory next to the test. On mismatch, actual + diff images are saved to `baselines/diffs/` for inspection.
