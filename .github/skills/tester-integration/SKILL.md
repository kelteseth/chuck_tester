---
name: tester-integration
description: 'Integrate tester into a Qt/QML application for automated testing. Use when: adding tester to an existing app, setting up CMake for tester, adding objectNames to QML for testability, configuring the TesterServer, troubleshooting connection issues.'
---

# Integrating Tester into a Qt/QML App

## When to Use

Use this skill when you need to make a Qt/QML application testable with chuck_tester. This covers:
- Adding the tester library to an existing app
- CMake configuration
- Adding objectNames to QML items
- Configuring the TesterServer
- Troubleshooting "can't connect" issues

## Step 1: CMake Setup

Add chuck as a subdirectory or installed package:

```cmake
# As subdirectory (tester source is in your repo)
add_subdirectory(path/to/tester)
target_link_libraries(myapp PRIVATE tester)

# As installed package
find_package(tester REQUIRED)
target_link_libraries(myapp PRIVATE tester::tester)
```

Required Qt modules:
```cmake
find_package(Qt6 REQUIRED COMPONENTS Quick Qml WebSockets Test)
```

Tester links `Qt6::Quick`, `Qt6::Qml` publicly and `Qt6::WebSockets`, `Qt6::Test` privately — you don't need to link those yourself unless your app also uses them directly.

## Step 2: C++ Integration

Add three lines after the QML engine loads:

```cpp
#include <tester/TesterServer.h>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    engine.loadFromModule("MyApp", "Main");

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());

    tester::TesterServer server(*window);
    server.start();

    return app.exec();
}
```

**Custom port:**
```cpp
tester::TesterServer server(*window, {.port = 9223});
```

**Important:** Create the TesterServer AFTER the engine loads and the window exists. The server must be on the stack (or have sufficient lifetime) to outlive `app.exec()`.

## Step 3: Add objectNames to QML

### What Needs a Name

Every item the test interacts with:
- Pages and views
- Buttons and actions
- Text fields and inputs
- Labels that contain verifiable content
- Checkboxes, switches, sliders
- StackView (for `busy` property waits)
- Dialog and Popup items

### Naming Convention

| Pattern | Examples |
|---------|---------|
| `pageName` + `Page` | `listPage`, `editPage`, `settingsPage` |
| `action` + `Btn` | `addBtn`, `saveBtn`, `cancelBtn`, `deleteBtn_0` |
| `name` + `Field` | `titleField`, `searchField`, `emailField` |
| `name` + `Label` | `statusLabel`, `label_0` |
| `check_N` | `check_0`, `check_1` |
| `stack` | The StackView itself |

### Static Items

```qml
Page {
    objectName: "listPage"

    ToolBar {
        Label { objectName: "headerTitle"; text: "My App" }
    }

    Button { objectName: "addBtn"; text: "Add" }
    TextField { objectName: "searchField" }
}
```

### Delegate Items (ListView, Repeater)

Use indexed names via the delegate's `id`:

```qml
ListView {
    objectName: "itemList"
    delegate: ItemDelegate {
        id: del
        objectName: "item_" + index
        required property int index
        required property string title

        contentItem: RowLayout {
            CheckBox { objectName: "check_" + del.index }
            Label { objectName: "label_" + del.index; text: del.title }
            ToolButton { objectName: "editBtn_" + del.index }
            ToolButton { objectName: "deleteBtn_" + del.index }
        }
    }
}
```

**Why `del.index` not `index`?** Children of the delegate don't have direct access to the `index` property. Qualifying through the delegate's `id` ensures the binding works at any nesting depth.

### StackView

```qml
StackView {
    objectName: "stack"
    initialItem: listPageComponent
}
```

The `busy` property is `true` during push/pop animations. Tests wait for `busy == false`.

### StackView.view on Nested Items

```qml
// ✗ Wrong — StackView.view is null on nested items
Button { onClicked: StackView.view.pop() }

// ✓ Correct — qualify through the page id
Page {
    id: editPage
    Button { onClicked: editPage.StackView.view.pop() }
}
```

### Popups and Dialogs

```qml
Dialog {
    objectName: "confirmDialog"
    Button { objectName: "dialogOkBtn" }
    Button { objectName: "dialogCancelBtn" }
}
```

## Step 4: Verify Connection

Start the app, then test the connection:

```bash
# REPL
uv run chuck repl --port 1408
chuck> tree depth=2
chuck> find interactive=True
```

You should see the QML tree with your objectNames.

## Troubleshooting

| Problem | Cause | Fix |
|---------|-------|-----|
| Can't connect | App not running or wrong port | Check the port in TesterServer config |
| No items in tree | TesterServer created before engine loads | Move TesterServer creation after `engine.loadFromModule()` |
| Item not found | Missing objectName | Add `objectName` to the QML item |
| "Item not visible" | Item on a different StackView page | Navigate to the correct page first |
| Header/footer items missing | Not using Page header/footer | Tester traverses `header` and `footer` QQuickItem properties |
