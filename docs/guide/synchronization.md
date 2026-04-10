# Synchronization

The biggest source of flaky tests is timing. Tester provides `wait()` as the primary synchronization primitive.

## Rule: Wait Before You Act

If an item might not exist yet, wait for it:

```python
# ✗ Flaky — item may not be created yet
await app.click("todoItem_0")

# ✓ Stable — wait, then act
await app.wait("todoItem_0", timeout=3000)
await app.click("todoItem_0")
```

## StackView Navigation

StackView animations mean the new page isn't ready immediately after `push()` or `pop()`. Wait for the `busy` property:

```python
STACK = "stack"

async def wait_nav(app):
    await app.wait(STACK, property="busy", value=False, timeout=3000)

# Every navigation needs a wait:
await app.click("addBtn")    # triggers stack.push()
await wait_nav(app)           # wait for animation
await app.keys("titleField", text="Hello")

await app.click("saveBtn")   # triggers stack.pop()
await wait_nav(app)           # wait for animation
```

!!! warning "Never disable animations"
    Don't set `StackView { popEnter: null; popExit: null }` to "fix" timing. Tests must work with real animations. Use `busy` waits instead.

## Model/Delegate Changes

When a ListView model changes (add/remove/reorder), delegates are recreated. The new delegate may not exist for a frame or two:

```python
# After adding an item:
await app.click("saveBtn")
await wait_nav(app)                          # StackView pop animation
await app.wait("todoItem_0", timeout=3000)   # delegate creation
props = await app.get("label_0", props="text")

# After deleting an item:
await app.click("deleteBtn_1")
# Don't immediately access deleteBtn_0 — re-layout takes a frame
await app.wait("deleteBtn_0", property="visible", value=True, timeout=3000)
```

## Property Waits

Wait for any property to reach a specific value:

```python
# Wait for visibility
await app.wait("myItem", property="visible", value=True, timeout=5000)

# Wait for enabled state
await app.wait("saveBtn", property="enabled", value=True, timeout=3000)

# Wait for text content
await app.wait("statusLabel", property="text", value="Done", timeout=5000)

# Wait for loading to finish
await app.wait("progressBar", property="visible", value=False, timeout=10000)
```

## Keyboard Shortcuts

`QTest::keySequence` queues events that Qt processes asynchronously. Tester includes a `processEvents()` call after shortcuts so QML Action handlers fire before the response returns. No extra waits needed:

```python
await app.keys(shortcut="Ctrl+S")  # save — handler fires before this returns
await app.keys(shortcut="Ctrl+L")  # load
```

## Timeouts

The default timeout for `wait()` is 5000ms. Adjust based on: 

- **Short animations** (StackView): 3000ms is usually enough
- **Network/file operations**: 5000–10000ms
- **App startup**: 5000–10000ms
- **Heavy computations**: increase as needed

```python
# Startup — give it time
await app.wait("mainPage", property="visible", value=True, timeout=10000)

# Quick animation
await app.wait("stack", property="busy", value=False, timeout=3000)
```

## Anti-Patterns

### Don't Use Sleep

```python
# ✗ Never
import asyncio
await asyncio.sleep(2)
await app.click("myBtn")

# ✓ Always use wait
await app.wait("myBtn", timeout=3000)
await app.click("myBtn")
```

### Don't Retry in a Loop

```python
# ✗ Brittle
for _ in range(10):
    try:
        await app.click("myBtn")
        break
    except ChuckError:
        await asyncio.sleep(0.5)

# ✓ Let tester handle it
await app.wait("myBtn", property="visible", value=True, timeout=5000)
await app.click("myBtn")
```

### Don't Assume Instant State Changes

```python
# ✗ Toggle may not have taken effect yet
await app.click("check_0")
# Reading immediately might get the old value if bindings haven't propagated

# ✓ For critical checks, wait for the property
await app.click("check_0")
await app.wait("check_0", property="checked", value=True, timeout=1000)
```
