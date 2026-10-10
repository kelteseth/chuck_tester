# QML Best Practices for Testability

## objectName Convention

Every item you want to test needs an `objectName`. Without it, tester can't find it.

### Static Items

```qml
Page { objectName: "listPage" }
Button { objectName: "addBtn" }
TextField { objectName: "titleField" }
StackView { objectName: "stack" }
ToolBar { objectName: "toolbar" }
```

### Delegate Items

Delegates are recreated as the model changes. Use indexed names:

```qml
ListView {
    objectName: "todoList"
    delegate: ItemDelegate {
        id: del
        objectName: "todoItem_" + index
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

!!! warning "Always qualify the index"
    Use `del.index` (via the delegate's `id`), not bare `index` or `model.index`. Nested children don't have direct access to the delegate's `index` property.

### Naming Conventions

| Pattern | Use for | Example |
|---------|---------|---------|
| `pageNamePage` | Pages | `listPage`, `editPage`, `settingsPage` |
| `actionBtn` | Buttons | `addBtn`, `saveBtn`, `cancelBtn`, `deleteBtn_0` |
| `nameField` | Text inputs | `titleField`, `searchField` |
| `nameLabel` / `label_N` | Labels | `statusLabel`, `label_0` |
| `check_N` | Checkboxes | `check_0`, `check_1` |
| `stack` | StackView | `stack` |

## StackView Navigation

### Qualify `StackView.view` on Nested Items

The `StackView.view` attached property only resolves on the item directly pushed to the StackView. If a button inside the page needs to pop, qualify it:

```qml
// ✗ Wrong — StackView.view is null on nested items
Button {
    onClicked: StackView.view.pop()
}

// ✓ Correct — qualify through the page's id
Page {
    id: editPage
    objectName: "editPage"

    Button {
        objectName: "cancelBtn"
        onClicked: editPage.StackView.view.pop()
    }
}
```

### Use `busy` for Synchronization

StackView's `busy` property is `true` during push/pop animations. Tests wait for `busy == false` after every navigation:

```python
await app.click("addBtn")  # triggers stack.push()
await app.wait("stack", property="busy", value=False, timeout=3000)
```

!!! tip "Never disable animations"
    It's tempting to set `StackView { popEnter: null; popExit: null }` to avoid timing issues. Don't — tests should work with real animations. Use `busy` waits instead.

## Headers and Footers

Tester traverses `header` and `footer` properties on QQuickItem. Items inside a `ToolBar` header are discoverable:

```qml
Page {
    header: ToolBar {
        Label {
            objectName: "headerTitle"
            text: "My App"
        }
    }
}
```

## Popups, Drawers, and Dialogs

`Popup`, `Drawer`, `Dialog`, and `Menu` derive from `QObject`, **not** `QQuickItem`. The scene-graph walk would skip them, but tester falls back to a QObject-children scan of the window — so an `objectName` on the popup itself **is** addressable for `get` and `wait`.

```qml
Drawer {
    id: navDrawer
    objectName: "navDrawer"    // discoverable even before the drawer opens
    edge: Qt.LeftEdge
    contentItem: Label {
        objectName: "drawerLabel"
        text: "Drawer content"
    }
}
```

```python
# Read state before opening
props = await app.get("navDrawer", props="opened,position")
assert props["opened"] is False

# Drive the UI, wait for the open transition
await app.click("openDrawerBtn")
await app.wait("navDrawer", property="opened", value=True, timeout=2000)
await app.wait("navDrawer", property="position", value=1.0, timeout=2000)

# Reach into the drawer's content (appears in the overlay tree once shown)
props = await app.get("drawerLabel", props="text")
```

### Inner items still need objectNames

The popup wrapper is addressable, but its children are still only discoverable if they have `objectName`s. Tag them too:

```qml
Dialog {
    id: confirmDialog
    objectName: "confirmDialog"
    standardButtons: Dialog.Ok | Dialog.Cancel
    contentItem: Label {
        objectName: "confirmDialogMessage"
        text: "Are you sure?"
    }
}
```

### What works on a popup (vs. what doesn't)

| Command | Works | Notes |
|---|---|---|
| `get`   | ✅ | Reads `opened`, `position`, `visible`, `title`, `modal`, any other `Q_PROPERTY` |
| `wait`  | ✅ | NOTIFY signal when available, polling fallback otherwise |
| `click`, `keys`, `drag`, `swipe`, `hover`, `scroll`, `screenshot` | ❌ | Popups aren't `QQuickItem`s; returns `-32010 NotInteractive`. Drive them via a UI button that calls `popup.open()` / `popup.close()`, or send `Escape` if `closePolicy` allows it |
| `find`, `tree` | ❌ | Tree commands walk scene-graph items only; popup wrappers don't appear |

### Closing a popup from a test

If your popup has a default `closePolicy` (most do — `CloseOnEscape | CloseOnPressOutside`), sending `Escape` works:

```python
await app.keys(key="escape")
# Wait for both 'opened' AND 'visible' to flip. 'opened' goes false when the close
# transition starts; 'visible' (and the modal state) only clears when the slide-out
# animation finishes. Subsequent clicks outside the popup are blocked until then.
await app.wait("navDrawer", property="opened", value=False, timeout=2000)
await app.wait("navDrawer", property="visible", value=False, timeout=2000)
```

Otherwise drive a close button that calls `popup.close()` from QML.

### Caveats

- The QObject fallback is **last resort**: if an `objectName` matches both a `QQuickItem` in the tree **and** a non-item QObject, the tree match wins. Don't reuse the same `objectName` for both.
- Multiple non-item QObjects sharing a name returns `-32005 AmbiguousItem`. Rename one.
