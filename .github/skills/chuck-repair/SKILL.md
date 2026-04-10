---
name: chuck-repair
description: 'Diagnose and fix failing chuck+tester integration tests. Use when: a test fails with a ChuckError, an assertion fails, a timeout occurs, a visual diff is detected, or any chuck test needs debugging/repair. Covers error diagnosis, context gathering, fix patterns, and re-verification.'
---

# Repairing Failing Chuck Tests

## When to Use This Skill

Use this skill when a chuck integration test fails — whether from CI output, manual run, or `chuck replay`. The goal is to diagnose the root cause, apply the minimal fix (to the test or the QML), and verify the fix works.

## Step 1: Gather Context

Before diagnosing, collect structured information about the failure.

### Run the test and capture output

```bash
# Run a single test file with verbose output
uv run pytest examples/myapp/test_myapp.py -v

# Or replay a JSONL session with step-level reporting
uv run chuck replay session.jsonl --fail-on-diff

# Or run via the test suite wrapper
uv run examples/myapp/test_suite.py
```

### Read the error

Every chuck failure is a `ChuckError` with a numeric code and message. Extract:
- **Error code** (e.g., `-32001`)
- **Error message** (e.g., `"Item not found: saveBtn"`)
- **Which test step failed** (the `await app.*()` call that raised)
- **What happened before** (did navigation complete? was a modal open?)

### Dump the QML tree at the failure point

If the test crashes mid-way, add a `tree()` call right before the failing line to see what's actually in the scene:

```python
# Temporarily add before the failing line:
items = await app.tree(depth=5)
for item in items:
    name = item.get("objectName", "")
    if name:
        print(f"  {item['path']}  objectName={name}  type={item['type']}")
```

Or use the REPL while the app is in the failed state:

```bash
uv run chuck repl --port 1408
chuck> tree depth=5
chuck> get myItem props=visible,enabled,x,y,width,height
```

### Take a screenshot

```python
await app.screenshot(save_to="debug_failure.png")
```

## Step 2: Diagnose by Error Code

### -32001: ItemNotFound

**The item's objectName doesn't exist in the QML tree.**

| Likely Cause | How to Confirm | Fix |
|---|---|---|
| objectName was renamed | Dump tree, search for similar names | Update the test to use the new name |
| Item hasn't been created yet | Check if a `wait()` precedes the action | Add `await app.wait("item", timeout=3000)` before the action |
| Item is on a different page | Check StackView current page | Navigate to the correct page first, wait for `busy=False` |
| Item is a delegate that was removed | Check ListView model count | Wait for the delegate to exist after model change |
| Item is inside a header/footer | Dump tree — tester traverses `header`/`footer` QQuickItem properties | Should be found; if not, check the header/footer is a QQuickItem |
| Item is in an overlay/popup | Item lives in the overlay tree | Check if a Popup/Dialog needs to be opened first |

**Common fix pattern:**
```python
# Before:
await app.click("saveBtn")  # -32001!

# After — add wait:
await app.wait("saveBtn", timeout=3000)
await app.click("saveBtn")
```

### -32002: ItemNotVisible

**The item exists but `isVisible()` is false.**

| Likely Cause | Fix |
|---|---|
| Item is on a hidden StackView page | Navigate to the page, `await wait_nav(app)` |
| Item is collapsed/hidden by a toggle | Expand the parent or toggle visibility |
| Item has `visible: false` explicitly | Check if this is intentional — may need a different item |
| Item is inside a closed Drawer/Popup | Open the Drawer/Popup first |

**Common fix pattern:**
```python
# Wait for the item to become visible before interacting:
await app.wait("myItem", property="visible", value=True, timeout=3000)
await app.click("myItem")
```

### -32003: ItemDisabled

**The item exists and is visible but `isEnabled()` is false.**

| Likely Cause | Fix |
|---|---|
| Form validation not met | Fill required fields first |
| Button disabled during loading | Wait for loading to finish |
| Item in a read-only state | Check app logic — may need a different workflow |

**Common fix pattern:**
```python
await app.wait("saveBtn", property="enabled", value=True, timeout=3000)
await app.click("saveBtn")
```

### -32004: ItemBlockedByModal

**A modal Dialog or Popup is covering the item.**

| Likely Cause | Fix |
|---|---|
| Unexpected error dialog | Dismiss the dialog first |
| Confirmation dialog opened | Click the confirm/cancel button |
| Previous step opened a modal that wasn't closed | Add a dismiss step |

**Common fix pattern:**
```python
# Dismiss the modal first:
await app.click("dialogCloseBtn")
# Then interact with the item behind it:
await app.click("targetBtn")
```

### -32005: AmbiguousItem

**Multiple items match the objectName or property query.**

| Likely Cause | Fix |
|---|---|
| Duplicate objectNames in QML | Fix the QML — objectNames should be unique |
| Generic name used in multiple delegates | Use indexed names (`"item_0"`, `"item_1"`) |
| Property query too broad | Add more properties to narrow the match |

### -32006: Timeout

**`wait()` timed out — the condition never became true.**

| Likely Cause | Fix |
|---|---|
| The item never appears | Check if the action that creates it actually ran |
| Property never reaches expected value | Use `get()` to read current value, check expectation |
| StackView animation didn't finish | Increase timeout or check that `busy` signal fires |
| Race condition | Add intermediate waits between actions |

**Debug pattern:**
```python
# Check what the current value is:
props = await app.get("myItem", props="visible,enabled")
print(f"Current state: {props}")
```

### -32007: PropertyNotFound

**The property name doesn't exist on the QML item.**

| Likely Cause | Fix |
|---|---|
| Typo in property name | Check QML source for correct spelling |
| Property is on a different type | Use `get()` without `props` to see all properties |

### -32008: ClickFailed

**Could not compute click coordinates.**

| Likely Cause | Fix |
|---|---|
| Item has zero width/height | Check layout — item may not be sized yet. Wait for layout. |
| Item is clipped outside viewport | Scroll to bring it into view first |

### -32009: ScreenshotFailed

**Screenshot capture failed.**

| Likely Cause | Fix |
|---|---|
| Window not yet visible | Wait for window to be shown |
| Item grab failed | Use full-window screenshot instead of item-specific |

## Step 3: Diagnose Assertion Failures

When the chuck command succeeds but a Python `assert` fails:

### Wrong text
```python
props = await app.get("label_0", props="text")
assert props["text"] == "Expected"  # AssertionError: "Actual" != "Expected"
```
**Fix:** Check if the app logic changed. Read the QML model code. Update the expected value if the behavior changed, or fix the test input.

### Wrong checked/enabled/visible state
```python
props = await app.get("check_0", props="checked")
assert props["checked"] is True  # AssertionError
```
**Fix:** The `click()` may not have toggled the state. Check:
1. Did the click land on the right item? (use `get()` to verify)
2. Is there a delay? (add `wait()` for the property)
3. Did the binding change in QML?

### Wrong item count
```python
await app.get("todoItem_1")  # ChuckError -32001 — expected 2 items but only 1 exists
```
**Fix:** The add/create action may have failed silently. Check the test flow — was the form saved correctly?

## Step 4: Fix Patterns

### Pattern: objectName Changed in QML

1. Dump the tree to find the new name
2. Update the test file — replace old name with new name
3. Check if the name is used in multiple places (e.g., `wait` + `click` + `get`)

### Pattern: Navigation Timing

Symptoms: intermittent `-32001` or `-32002` after a push/pop.

```python
# Before (flaky):
await app.click("addBtn")
await app.keys("titleField", text="Hello")

# After (stable):
await app.click("addBtn")
await app.wait(STACK, property="busy", value=False, timeout=3000)
await app.keys("titleField", text="Hello")
```

### Pattern: Model Update Timing

Symptoms: delegate items not found after add/remove.

```python
# Before (flaky):
await app.click("saveBtn")
await app.get("todoItem_0", props="text")

# After (stable):
await app.click("saveBtn")
await wait_nav(app)  # if StackView pop involved
await app.wait("todoItem_0", timeout=3000)
props = await app.get("todoItem_0", props="text")
```

### Pattern: QML Needs Fixing (Not the Test)

Sometimes the test is correct but the QML has a bug:

- **Missing objectName** — add `objectName: "myItem"` to the QML item
- **Wrong binding** — fix the QML property binding
- **StackView.view.pop() on nested item** — must qualify: `editPage.StackView.view.pop()`
- **Delegate index not qualified** — use `objectName: "item_" + del.index` (via delegate `id`)

When fixing QML, verify the fix doesn't break other tests.

### Pattern: Visual Regression

When `screenshot(verify=True)` fails with a visual mismatch:

1. Look at the diff image in `baselines/diffs/<tag>.diff.png`
2. Look at the actual image in `baselines/diffs/<tag>.actual.png`
3. Compare with the baseline in `baselines/<tag>.png`
4. Decide:
   - **Intentional change** → update the baseline: `uv run chuck baseline session.jsonl`
   - **Regression** → fix the QML that caused the visual change
   - **Flaky rendering** → adjust the SSIM threshold or add mask regions

## Step 5: Apply Fix and Verify

1. Edit the test file (or QML file) with the minimal fix
2. Re-run the test:
   ```bash
   uv run pytest examples/myapp/test_myapp.py -v
   ```
3. Confirm it passes
4. If fixing QML, run ALL tests for that app to check for regressions
5. Commit with a descriptive message explaining the root cause

## Step 6: CI Integration

### Using `chuck replay` in CI

```bash
# Run replay with structured error output
uv run chuck replay session.jsonl --fail-on-diff

# Exit code: 0 = pass, 1 = fail
# Output includes step number, error code, and message for each failure
```

### Using `chuck repair` (AI-assisted)

```bash
# Replay, and on failure dump context for an AI agent to consume
uv run chuck repair session.jsonl --port 1408
```

This command:
1. Replays the session
2. On first failure, captures: error details, QML tree dump, screenshot
3. Writes a structured report to `repair_context.json`
4. An AI agent (via MCP or Copilot skill) reads the report and applies fixes

### Structured Failure Report Format

```json
{
  "session": "session.jsonl",
  "failed_step": 5,
  "total_steps": 12,
  "error": {
    "code": -32001,
    "message": "Item not found: saveBtn"
  },
  "completed_steps": [
    {"step": 1, "action": "wait", "item": "mainWindow", "status": "ok"},
    {"step": 2, "action": "click", "item": "addBtn", "status": "ok"}
  ],
  "tree_dump": "repair_context_tree.json",
  "screenshot": "repair_context_screenshot.png"
}
```

## Quick Reference: Error → First Thing to Try

| Error | First Action |
|---|---|
| `-32001` ItemNotFound | Add `wait(item, timeout=3000)` before the failing step |
| `-32002` ItemNotVisible | Add `wait(item, property="visible", value=True)` |
| `-32003` ItemDisabled | Add `wait(item, property="enabled", value=True)` |
| `-32004` BlockedByModal | Find and dismiss the modal/dialog first |
| `-32005` AmbiguousItem | Use indexed delegate names or fix duplicate objectNames |
| `-32006` Timeout | Check the expected value with `get()`, fix expectation |
| `-32007` PropertyNotFound | Check property spelling, use `get()` without props to list all |
| `-32008` ClickFailed | Wait for layout (`width > 0`), scroll into view |
| Assertion failure | Read current state with `get()`, compare with expected |
| Visual mismatch | Check diff image, update baseline or fix QML |
