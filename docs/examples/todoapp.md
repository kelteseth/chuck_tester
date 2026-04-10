# Example: Todo App

A complete Material Design todo app with StackView navigation, tested with chuck_tester.

## App Structure

```
examples/todoapp/
├── CMakeLists.txt
├── main.cpp                 # App entry + tester setup
├── Todo.h / Todo.cpp       # Todo model
├── Main.qml                # StackView + shortcuts
├── TodoListPage.qml        # List with delegates
├── TodoEditPage.qml        # Add/edit form
├── qtquickcontrols2.conf   # Material theme
├── test_todoapp.py         # Integration test
└── test_suite.py           # Runner (start/test/cleanup)
```

## C++ Side

```cpp title="main.cpp"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <tester/TesterServer.h>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("chuck");
    app.setApplicationName("todoapp");
    QQuickStyle::setStyle("Material");

    QQmlApplicationEngine engine;
    engine.loadFromModule("TodoApp", "Main");

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    tester::TesterServer server(*window);
    server.start();

    return app.exec();
}
```

## QML objectNames

Key items and their objectNames:

| objectName | Type | Page |
|------------|------|------|
| `stack` | StackView | Main |
| `listPage` | Page | List |
| `addBtn` | RoundButton (FAB) | List |
| `todoItem_N` | ItemDelegate | List (delegate) |
| `check_N` | CheckBox | List (delegate) |
| `label_N` | Label | List (delegate) |
| `editBtn_N` | ToolButton | List (delegate) |
| `deleteBtn_N` | ToolButton | List (delegate) |
| `editPage` | Page | Edit |
| `titleField` | TextField | Edit |
| `saveBtn` | Button | Edit |
| `cancelBtn` | Button | Edit |

## The Test

```python title="test_todoapp.py"
import pytest
from chuck import ChuckClient

STACK = "stack"

async def wait_nav(app: ChuckClient):
    await app.wait(STACK, property="busy", value=False, timeout=3000)

@pytest.mark.asyncio
async def test_todoapp():
    async with ChuckClient(port=1408) as app:
        await app.wait("listPage", property="visible", value=True, timeout=5000)

        # Add a todo
        await app.click("addBtn")
        await wait_nav(app)
        await app.keys("titleField", text="Buy milk")
        await app.click("saveBtn")
        await wait_nav(app)
        await app.wait("todoItem_0", timeout=3000)

        # Toggle it
        await app.click("check_0")
        props = await app.get("check_0", props="checked")
        assert props["checked"] is True

        # Edit it
        await app.click("editBtn_0")
        await wait_nav(app)
        await app.keys("titleField", shortcut="Ctrl+A")
        await app.keys("titleField", text="Buy oat milk")
        await app.click("saveBtn")
        await wait_nav(app)
        props = await app.get("label_0", props="text")
        assert props["text"] == "Buy oat milk"

        # Cancel an edit (should not save)
        await app.click("editBtn_0")
        await wait_nav(app)
        await app.keys("titleField", shortcut="Ctrl+A")
        await app.keys("titleField", text="SHOULD NOT SAVE")
        await app.click("cancelBtn")
        await wait_nav(app)
        props = await app.get("label_0", props="text")
        assert props["text"] == "Buy oat milk"

        # Save and load
        await app.keys(shortcut="Ctrl+S")
        await app.keys(shortcut="Ctrl+L")
        await app.wait("todoItem_0", timeout=3000)
        props = await app.get("label_0", props="text")
        assert props["text"] == "Buy oat milk"

        await app.quit()
```

## Running

```bash
# Build the app
cmake --preset <your-preset>
cmake --build --preset <your-build-preset> --target todoapp

# Run the test (starts app, runs test, cleans up)
uv run examples/todoapp/test_suite.py
```

## What the Test Covers

| Feature | How it's tested |
|---------|----------------|
| StackView navigation | `addBtn` → push, `saveBtn`/`cancelBtn` → pop, `wait_nav()` |
| Form input | `keys()` with text and `Ctrl+A` select-all |
| Toggle state | `click()` checkbox, `get()` checked property |
| Cancel without saving | Edit, change text, cancel, verify original text |
| Persistence | `Ctrl+S` save, delete, `Ctrl+L` load, verify data restored |
| Delegate indexing | `todoItem_0`, `check_0`, `label_0`, `editBtn_0`, `deleteBtn_0` |
