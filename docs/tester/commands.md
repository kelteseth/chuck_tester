# Tester Commands

All communication uses [JSON-RPC 2.0](https://www.jsonrpc.org/specification) over WebSocket. Every request has `jsonrpc`, `id`, `method`, and `params`. Every response has `jsonrpc`, `id`, and either `result` or `error`.

## Command Reference

### click

Click an item.

```json
{"jsonrpc": "2.0", "id": 1, "method": "click", "params": {
    "item": "saveBtn"
}}
```

| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `item` | string | required | objectName of the item |
| `button` | string | `"left"` | `"left"`, `"right"`, or `"middle"` |
| `double` | bool | `false` | Double-click |
| `hold` | int | — | Hold duration in ms before release |
| `root` | string | — | objectName to scope the search |

### keys

Type text, send a key, or trigger a shortcut.

```json
{"jsonrpc": "2.0", "id": 2, "method": "keys", "params": {
    "item": "nameField",
    "text": "Hello world"
}}
```

| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `item` | string | — | Target item (optional for global shortcuts) |
| `text` | string | — | Text to type character by character |
| `key` | string | — | Special key: `"Return"`, `"Escape"`, `"Tab"`, `"Backspace"`, etc. |
| `shortcut` | string | — | Keyboard shortcut: `"Ctrl+S"`, `"Ctrl+A"`, `"Ctrl+Z"`, etc. |
| `root` | string | — | objectName to scope the search |

Exactly one of `text`, `key`, or `shortcut` must be provided.

### get

Read properties of an item.

```json
{"jsonrpc": "2.0", "id": 3, "method": "get", "params": {
    "item": "nameField",
    "props": "text,enabled"
}}
```

| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `item` | string | required | objectName of the item |
| `props` | string | — | Comma-separated property names. Omit to get all. |

### wait

Wait for an item to exist or a property to reach a value.

```json
{"jsonrpc": "2.0", "id": 4, "method": "wait", "params": {
    "item": "loadingSpinner",
    "property": "visible",
    "value": false,
    "timeout": 5000
}}
```

| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `item` | string | required | objectName to wait for |
| `property` | string | — | Property to watch |
| `value` | any | — | Expected value |
| `timeout` | int | `5000` | Timeout in milliseconds |

If `property` is omitted, waits for the item to exist in the tree. If provided, waits for the property to equal `value`.

### find

Search the QML tree.

| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `type` | string | — | Filter by QML type name |
| `interactive` | bool | `false` | Only items that accept input |
| `accessible` | bool | `false` | Only items with Accessible metadata |
| `depth` | int | — | Max tree depth |
| `root` | string | — | Root objectName to search from |
| `props` | string | — | Properties to include per item |

### tree

Dump the QML tree as a flat list.

| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `root` | string | — | Root objectName |
| `depth` | int | — | Max depth |
| `props` | string | — | Properties to include per item |

### screenshot

Capture a screenshot.

| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `item` | string | — | Crop to this item. Omit for full window. |

Returns `width`, `height`, `format` (`"png"`), and `data` (base64-encoded PNG).

### drag

Drag from one item to another, or by a pixel offset.

| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `from` | string | required | Source item objectName |
| `to` | string | — | Target item objectName. Exclusive with `by`. |
| `by` | [int, int] | — | `[dx, dy]` offset from the source's center. Exclusive with `to`. |
| `steps` | int | `8` | Intermediate mouse moves (2–100) |
| `root` | string | — | objectName to scope the search |

### swipe

Swipe/scroll gesture.

| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `item` | string | required | Item to swipe on |
| `direction` | string | `"up"` | `"up"`, `"down"`, `"left"`, `"right"` |
| `distance` | int | `200` | Pixels |

### hover

Move the mouse with no button pressed, to trigger `HoverHandler`, `MouseArea { hoverEnabled: true }` and the `hovered` property of controls.

```json
{"jsonrpc": "2.0", "id": 5, "method": "hover", "params": {
    "item": "row_3"
}}
```

| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `item` | string | — | Item to hover (its center). Exclusive with `point`. |
| `by` | [int, int] | — | `[dx, dy]` offset from the item's center |
| `point` | [int, int] | — | `[x, y]` window coordinates instead of an item |
| `root` | string | — | objectName to scope the search |

There is no "unhover": hover another item or a `point` on the background to end a hover.

### scroll

Turn the mouse wheel over an item.

```json
{"jsonrpc": "2.0", "id": 6, "method": "scroll", "params": {
    "item": "resultList", "direction": "down", "steps": 3
}}
```

| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `item` | string | required | Item under the wheel |
| `direction` | string | `"down"` | `"up"`, `"down"`, `"left"`, `"right"` |
| `steps` | int | `1` | Wheel notches (1–100) |
| `modifiers` | string | — | Held modifiers, e.g. `"Ctrl"` or `"Ctrl+Shift"` |
| `root` | string | — | objectName to scope the search |

`Flickable`/`ListView` animate wheel scrolling, so the command returns before the content has stopped. `wait` for `moving` to be `false` before reading `contentY`.

### window

Change the window's state, flags or geometry. With no params it only reports them.

```json
{"jsonrpc": "2.0", "id": 7, "method": "window", "params": {
    "width": 800, "height": 600
}}
```

| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `state` | string | — | `"normal"`, `"maximized"`, `"minimized"`, `"fullscreen"` |
| `frameless` | bool | — | Remove (`true`) or restore (`false`) the title bar and borders |
| `stayOnTop` | bool | — | Keep the window above other windows |
| `width`, `height` | int | current | New client size. Restores a maximized/fullscreen window first. |
| `x`, `y` | int | current | New position in screen coordinates |

Size and position can only be combined with `state: "normal"` (or no `state`). Toggling `frameless` keeps the content size.

Returns what the window actually ended up as. The window manager or the window's min/max size may clamp a request, so compare against the result rather than the request:

```json
{"state": "normal", "frameless": false, "stayOnTop": false, "visible": true,
 "x": 100, "y": 120, "width": 800, "height": 600}
```

### set

Write a property on an item, singleton, popup or the window itself.

```json
{"jsonrpc": "2.0", "id": 8, "method": "set", "params": {
    "item": "@MyApp/Theme", "property": "dark", "value": true
}}
```

| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `item` | string | required | objectName or singleton identifier |
| `property` | string | required | Property name. Grouped names like `"font.pixelSize"` work. |
| `value` | any | required | New value. Converted to the property's type (int, enum, color string, ...). |

Returns `previous` and the new `value`. Like an assignment in QML, writing a property **removes its binding**.

### invoke

Call a QML function, `Q_INVOKABLE`, slot or signal.

```json
{"jsonrpc": "2.0", "id": 9, "method": "invoke", "params": {
    "item": "root", "method": "openPage", "args": ["settings"]
}}
```

| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `item` | string | required | objectName or singleton identifier |
| `method` | string | required | Method name |
| `args` | array | `[]` | Positional arguments |

Returns `result`: the return value, or `{objectName, type}` when the method returns a `QObject`. An exception thrown by the function comes back as error `-32012`.

`set` and `invoke` run app code on behalf of the client. Set `TesterServer::Config::allowScripting = false` to disable both; they then fail with `-32011`.

### version

Report what this tester supports. Chuck calls it on connect. No parameters.

```json
{"protocol": 2, "tester": "0.2", "qt": "6.12.0", "scripting": true,
 "methods": ["version", "find", "get", "click", "..."]}
```

`protocol` goes up whenever methods are added or change shape. `methods` is the authoritative list. Testers from before protocol 2 don't have `version` and answer with `-32601`.

### quit

Gracefully close the application. No parameters.

## Error Codes

```json
{"jsonrpc": "2.0", "id": 1, "error": {
    "code": -32001,
    "message": "Item not found: loginButton"
}}
```

| Code | Name | Meaning |
|------|------|---------|
| `-32001` | ItemNotFound | No item with that objectName exists in the tree |
| `-32002` | ItemNotVisible | Item exists but `isVisible()` is false |
| `-32003` | ItemDisabled | Item exists but `isEnabled()` is false |
| `-32004` | ItemBlockedByModal | A modal popup is covering the item |
| `-32005` | AmbiguousItem | Multiple items match the query |
| `-32006` | Timeout | `wait()` timed out before the condition was met |
| `-32007` | PropertyNotFound | The requested property doesn't exist on the item |
| `-32008` | ClickFailed | Could not compute click coordinates (zero-size item) |
| `-32009` | ScreenshotFailed | Screenshot capture failed |
| `-32010` | NotInteractive | Input command aimed at a non-item (singleton, popup, the window) |
| `-32011` | ScriptingDisabled | `set`/`invoke` while `Config::allowScripting` is false |
| `-32012` | InvokeFailed | `invoke` threw, or no QML engine was available |
| `-32601` | MethodNotFound | Unknown method. Chuck raises it locally too, for methods the app's tester doesn't support. |
