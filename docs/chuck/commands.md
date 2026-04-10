# Chuck Commands

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

Drag from one item to another.

| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `from` | string | required | Source item objectName |
| `to` | string | required | Target item objectName |

### swipe

Swipe/scroll gesture.

| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `item` | string | required | Item to swipe on |
| `direction` | string | `"up"` | `"up"`, `"down"`, `"left"`, `"right"` |
| `distance` | int | `200` | Pixels |

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
