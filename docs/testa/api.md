# Testa Client API

## TestaClient

The main interface for driving a chuck-enabled app.

```python
from testa import TestaClient

async with TestaClient(host="127.0.0.1", port=1408) as app:
    ...
```

### Constructor

```python
TestaClient(
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
await app.drag(from_item: str, to_item: str, *, root: str = None)
```

### swipe

```python
await app.swipe(item: str, *, direction: str = "up", distance: int = 200)
```

### quit

```python
await app.quit()
```

## ChuckError

All chuck errors are raised as `ChuckError`:

```python
from testa import ChuckError

try:
    await app.click("missingBtn")
except ChuckError as e:
    print(e.code)     # -32001
    print(e.message)  # "Item not found: missingBtn"
    print(e.data)     # optional extra context
```

See [Error Codes](../chuck/commands.md#error-codes) for the full list.

## Low-Level Client

For advanced use, access the raw JSON-RPC client:

```python
result = await app.raw.call("someMethod", param1="value")
```
