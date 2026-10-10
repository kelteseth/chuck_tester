# Chuck Client API

## ChuckClient

The main interface for driving a tester-enabled app.

```python
from chuck import ChuckClient

async with ChuckClient(host="127.0.0.1", port=1408) as app:
    ...
```

### Constructor

```python
ChuckClient(
    host: str = "127.0.0.1",
    port: int = 1408,
    *,
    uri: str | None = None,          # override full WebSocket URI
    baseline_dir: str | Path = "baselines",  # for screenshot baselines
)
```

### click

```python
await app.click(
    item: str,               # objectName
    *,
    button: str = None,      # "left", "right", "middle"
    double: bool = False,    # double-click
    hold: int = None,        # hold duration in ms
    root: str = None,        # scope search to this objectName
)
```

### keys

```python
await app.keys(
    item: str = None,        # target item (optional for shortcuts)
    *,
    text: str = None,        # type this text
    key: str = None,         # special key ("Return", "Escape", etc.)
    shortcut: str = None,    # shortcut ("Ctrl+S", "Ctrl+A", etc.)
    root: str = None,
)
```

Provide exactly one of `text`, `key`, or `shortcut`.

### get

```python
result = await app.get(
    item: str,               # objectName
    *,
    props: str = None,       # comma-separated: "text,enabled,visible"
) -> dict[str, Any]
```

Returns a dict of property names to values.

### wait

```python
await app.wait(
    item: str,               # objectName
    *,
    property: str = None,    # property to watch
    value: Any = None,       # expected value
    timeout: int = 5000,     # ms
)
```

- Without `property`: waits for the item to exist in the tree
- With `property` and `value`: waits for `item.property == value`
- Raises `ChuckError(-32006)` on timeout

### find

```python
items = await app.find(
    *,
    type: str = None,        # QML type name
    interactive: bool = False,
    accessible: bool = False,
    depth: int = None,
    root: str = None,
    props: str = None,
) -> list[dict]
```

### tree

```python
items = await app.tree(
    *,
    root: str = None,
    depth: int = None,
    props: str = None,
) -> list[dict]
```

### screenshot

```python
result = await app.screenshot(
    item: str = None,             # crop to item, or full window
    *,
    tag: str = None,              # baseline label
    verify: bool = False,         # compare against baseline
    save_to: str | Path = None,   # save PNG to path
    method: str = "ssim",         # "ssim", "phash", "exact"
    threshold: float = 0.95,
    mask_regions: list[tuple] = None,
) -> dict
```

When `verify=True` and a baseline exists, returns `match` (bool) and `score` (float). When no baseline exists, creates one automatically.

### drag

```python
await app.drag(from_item: str, to_item: str = None, *, by: tuple[int, int] = None,
               steps: int = None, root: str = None)
```

Exactly one of `to_item` or `by` (a `(dx, dy)` offset from the source's center).

### swipe

```python
await app.swipe(item: str, *, direction: str = "up", distance: int = 200)
```

### hover

```python
await app.hover(item: str = None, *, by: tuple[int, int] = None,
                point: tuple[int, int] = None, root: str = None)
```

Moves the mouse with no button held. Give `item` (optionally offset `by`) or a window `point`. Hover something else to end a hover.

```python
await app.hover("row_3")
await app.wait("detailsPanel", property="title", value="Row 3")
```

### scroll

```python
await app.scroll(item: str, *, direction: str = "down", steps: int = 1,
                 modifiers: str = None, root: str = None)
```

Mouse-wheel notches over `item`. Flickables animate, so wait for `moving == False` before asserting on `contentY`.

### window / resize

```python
await app.window(*, state: str = None, frameless: bool = None, stay_on_top: bool = None,
                 width: int = None, height: int = None, x: int = None, y: int = None) -> dict
await app.resize(width: int, height: int) -> dict   # shorthand for window(width=, height=)
```

`state` is `"normal"`, `"maximized"`, `"minimized"` or `"fullscreen"`. Returns the actual state and geometry afterwards; `await app.window()` just reads them.

```python
for w, h in [(360, 640), (1280, 800)]:
    await app.resize(w, h)
    await app.screenshot(tag=f"layout_{w}x{h}", verify=True)
```

### set

```python
await app.set(item: str, property: str, value) -> dict
```

Writes a property (grouped names like `"font.pixelSize"` work) and returns `previous` and `value`. Removes any binding on it. Use it to put the app into a state directly instead of clicking there:

```python
await app.set("@MyApp/Theme", "dark", True)
```

### invoke

```python
result = await app.invoke(item: str, method: str, *args)
```

Calls a QML function, `Q_INVOKABLE`, slot or signal and returns its result.

```python
await app.invoke("root", "openPage", "components")
```

`set` and `invoke` can be disabled in the app with `TesterServer::Config::allowScripting = false`.

### quit

```python
await app.quit()
```

## Version check

On connect, `ChuckClient` asks the app's tester what it supports (`app.server`):

```python
async with ChuckClient() as app:
    print(app.server)  # {"protocol": 2, "tester": "0.2", "qt": "6.12.0", "methods": [...], ...}
```

- If the tester is older than chuck, a warning is emitted and commands it lacks raise `ChuckError(-32601, "Cannot call 'hover': ... rebuild the app against a newer tester")` without being sent. Without this check, an old tester would ignore params it doesn't know.
- If the tester is newer than chuck, a warning suggests updating chuck.

## ChuckError

All chuck errors are raised as `ChuckError`:

```python
from chuck import ChuckError

try:
    await app.click("missingBtn")
except ChuckError as e:
    print(e.code)     # -32001
    print(e.message)  # "Item not found: missingBtn"
    print(e.data)     # optional extra context
```

See [Error Codes](../tester/commands.md#error-codes) for the full list.

## Low-Level Client

For advanced use, access the raw JSON-RPC client:

```python
result = await app.raw.call("someMethod", param1="value")
```