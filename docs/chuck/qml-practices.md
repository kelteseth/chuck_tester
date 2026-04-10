# QML Best Practices for Testability

## objectName Convention

Every item you want to test needs an `objectName`. Without it, chuck can't find it.

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

Chuck traverses `header` and `footer` properties on QQuickItem. Items inside a `ToolBar` header are discoverable:

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

## Popups and Dialogs

Popups live in the overlay tree, separate from the main content tree. Chuck searches both trees. Give your dialogs objectNames:

```qml
Dialog {
    objectName: "confirmDialog"

    Label { objectName: "dialogMessage" }

    footer: DialogButtonBox {
        Button { objectName: "dialogOk"; text: "OK" }
        Button { objectName: "dialogCancel"; text: "Cancel" }
    }
}
```
