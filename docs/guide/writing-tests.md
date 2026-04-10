# Writing Tests

Tests are regular async Python functions using pytest and pytest-asyncio.

## Test File Structure

```python
import pytest
from testa import TestaClient

STACK = "stack"  # StackView objectName — declare once


async def wait_nav(app: TestaClient):
    """Wait for StackView animation to finish."""
    await app.wait(STACK, property="busy", value=False, timeout=3000)


@pytest.mark.asyncio
async def test_my_feature():
    async with TestaClient(port=1408) as app:
        # Wait for app to be ready
        await app.wait("listPage", property="visible", value=True, timeout=5000)

        # Test steps...
        await app.click("myButton")
        props = await app.get("myLabel", props="text")
        assert props["text"] == "expected"

        await app.quit()
```

### Key Rules

1. **One `TestaClient` per test** — the async context manager connects and disconnects
2. **Always wait first** — `wait()` for the initial page before interacting
3. **All commands are awaited** — they're async JSON-RPC calls
4. **End with `quit()`** — close the app gracefully
5. **No subprocess management in the test** — the app is started separately (or by a test suite wrapper)

## Running Tests

```bash
# App must be running first
uv run pytest examples/todoapp/test_todoapp.py -v
```

Or use a test suite wrapper:

```bash
uv run examples/todoapp/test_suite.py
```

## Assertions

Chuck commands return dicts. Assert on specific properties:

```python
# Text content
props = await app.get("titleLabel", props="text")
assert props["text"] == "Expected Title"

# Checkbox state
props = await app.get("check_0", props="checked")
assert props["checked"] is True

# Visibility
props = await app.get("myItem", props="visible")
assert props["visible"] is True

# Item existence (raises ChuckError -32001 if missing)
from testa import ChuckError
with pytest.raises(ChuckError):
    await app.get("deletedItem")
```

## Test Suite Pattern

Keep the actual test logic in `test_myapp.py` and the lifecycle management in a separate `test_suite.py`:

```python title="test_suite.py"
import os, subprocess, sys, time
from pathlib import Path

SAVE_FILE = Path(os.environ["LOCALAPPDATA"]) / "myapp" / "data.json"

def find_exe():
    candidates = sorted(Path(".").glob("build/*/myapp/myapp.exe"))
    if not candidates:
        sys.exit("exe not found")
    return candidates[-1]

def main():
    exe = find_exe()
    proc = None
    rc = 1
    try:
        SAVE_FILE.unlink(missing_ok=True)
        proc = subprocess.Popen([str(exe)])
        time.sleep(2)
        rc = subprocess.call(
            [sys.executable, "-m", "pytest", "test_myapp.py", "-v"]
        )
    finally:
        if proc:
            proc.terminate()
            proc.wait(timeout=10)
        SAVE_FILE.unlink(missing_ok=True)
    sys.exit(rc)

if __name__ == "__main__":
    main()
```

## Helper Functions

Extract common patterns into helpers at the top of the test file:

```python
STACK = "stack"

async def wait_nav(app):
    """Wait for StackView push/pop to finish."""
    await app.wait(STACK, property="busy", value=False, timeout=3000)

async def add_item(app, title):
    """Add an item via the edit page."""
    await app.click("addBtn")
    await wait_nav(app)
    await app.keys("titleField", text=title)
    await app.click("saveBtn")
    await wait_nav(app)
```

## Debugging

### Dump the Tree

```python
items = await app.tree(depth=5)
for item in items:
    name = item.get("objectName", "")
    if name:
        print(f"  {item['path']}  name={name}  type={item['type']}")
```

### Save a Screenshot

```python
await app.screenshot(save_to="debug.png")
```

### Check Current State

```python
props = await app.get("myItem", props="visible,enabled,x,y,width,height")
print(props)
```
