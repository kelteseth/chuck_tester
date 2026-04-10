---
name: chuck-visual
description: 'Visual regression testing with chuck+tester. Use when: setting up screenshot baselines, comparing screenshots, tuning SSIM or pHash thresholds, creating mask regions to ignore dynamic content, setting up visual testing in CI, debugging visual diff failures.'
---

# Visual Regression Testing with Chuck

## When to Use

Use this skill when you need to:
- Capture screenshot baselines for a Qt/QML app
- Compare screenshots across runs to detect visual regressions
- Handle dynamic content (timestamps, animations) with masks
- Tune comparison thresholds for a specific UI
- Set up visual testing in CI pipelines
- Diagnose a visual diff failure

## Architecture

```
BaselineStore (screenshots.py)
├── baseline/        ← Reference screenshots (committed)
├── current/         ← Latest capture (git-ignored)
└── diff/            ← Visual diffs when mismatch (git-ignored)

visual_diff.py
├── compare_ssim()   ← Structural similarity (scikit-image)
├── compare_phash()  ← Perceptual hash (imagehash)
└── diff_image()     ← Red-highlighted pixel diff
```

## Capturing Baselines

### Via Python Test

```python
async def test_visual_baseline(chuck: ChuckClient):
    """Capture or verify the list page screenshot."""
    # Navigate to the target state
    await chuck.wait_for_property("stack", "busy", False)

    # Capture a screenshot as PNG bytes
    png_bytes = await chuck.screenshot()

    # Save as baseline using BaselineStore
    from chuck.screenshots import BaselineStore
    store = BaselineStore("baselines")
    store.save_baseline("list_page", png_bytes)
```

### Via CLI

```bash
# Capture a one-off screenshot
uv run chuck repl --port 1408
chuck> screenshot
# Saves to screenshots/screenshot_<timestamp>.png
```

### Via MCP

When using chuck's MCP server, the AI can call `chuck_screenshot` to capture the current state. This is useful for interactive baseline creation.

## Comparison Methods

### SSIM (Structural Similarity Index)

Best for detecting **layout changes**, **color shifts**, **missing elements**.

```python
from chuck.visual_diff import compare_ssim

score = compare_ssim(baseline_bytes, current_bytes)
# score: 0.0 (completely different) to 1.0 (identical)

assert score > 0.95, f"Visual regression: SSIM={score:.4f}"
```

**Threshold guidance:**
| Threshold | Use Case |
|-----------|----------|
| `>0.99` | Pixel-perfect match (no dynamic content) |
| `>0.95` | Standard UI with minor rendering variance |
| `>0.90` | UI with some dynamic content masked out |
| `>0.85` | Loose check, only catch major regressions |

Requires `scikit-image`:
```bash
pip install scikit-image
```

### Perceptual Hash (pHash)

Best for detecting **structural changes** while ignoring minor color/rendering differences.

```python
from chuck.visual_diff import compare_phash

distance = compare_phash(baseline_bytes, current_bytes)
# distance: 0 (identical) to ~64 (completely different)

assert distance < 5, f"Visual regression: pHash distance={distance}"
```

**Distance guidance:**
| Distance | Meaning |
|----------|---------|
| `0` | Identical structure |
| `1-3` | Sub-pixel rendering differences |
| `4-8` | Minor layout changes |
| `>8` | Significant visual change |

Requires `imagehash` and `Pillow`:
```bash
pip install imagehash Pillow
```

### Exact Comparison

For cases where pixel-perfect matching is required:

```python
assert baseline_bytes == current_bytes, "Screenshots differ"
```

**Caution:** Exact matching is fragile. Font rendering, anti-aliasing, and DPI scaling can cause false positives across machines.

## Handling Dynamic Content

### Mask Regions

Create a mask to ignore specific areas (clocks, counters, animation frames):

```python
from chuck.visual_diff import compare_ssim
from PIL import Image
import io
import numpy as np

def mask_region(png_bytes: bytes, regions: list[tuple[int,int,int,int]]) -> bytes:
    """Zero out rectangular regions (x, y, w, h) in an image."""
    img = Image.open(io.BytesIO(png_bytes))
    arr = np.array(img)
    for x, y, w, h in regions:
        arr[y:y+h, x:x+w] = 0
    masked = Image.fromarray(arr)
    buf = io.BytesIO()
    masked.save(buf, format="PNG")
    return buf.getvalue()

# Apply same mask to both images
mask = [(10, 10, 200, 30)]  # Ignore a timestamp region
score = compare_ssim(
    mask_region(baseline, mask),
    mask_region(current, mask)
)
```

### Wait for Stable State

Before capturing, ensure the UI is quiescent:

```python
# Wait for animations to complete
await chuck.wait_for_property("stack", "busy", False)

# Wait for data to load
await chuck.wait_for_property("statusLabel", "text", "Ready")

# Small delay for rendering to settle (last resort)
import asyncio
await asyncio.sleep(0.1)
```

## Generating Diff Images

When a comparison fails, generate a visual diff:

```python
from chuck.visual_diff import diff_image
from pathlib import Path

diff_bytes = diff_image(baseline_bytes, current_bytes)
Path("test-output/diff.png").write_bytes(diff_bytes)
```

The diff image highlights changed pixels in red, making it easy to spot regressions.

## BaselineStore Management

```python
from chuck.screenshots import BaselineStore

store = BaselineStore("baselines")

# Save a baseline (overwrites if exists)
store.save_baseline("list_page", png_bytes)

# Load a baseline
baseline = store.load_baseline("list_page")

# Check if a baseline exists
if store.has_baseline("list_page"):
    ...
```

Directory structure after saving:
```
baselines/
├── baseline/
│   └── list_page.png
├── current/
│   └── list_page.png      ← written during comparison
└── diff/
    └── list_page_diff.png  ← written when mismatch detected
```

## CI Integration

### .gitignore
```
baselines/current/
baselines/diff/
```

### GitHub Actions

```yaml
- name: Run visual tests
  run: uv run pytest tests/ -k visual

- name: Upload visual diffs
  if: failure()
  uses: actions/upload-artifact@v4
  with:
    name: visual-diffs
    path: baselines/diff/
```

### Updating Baselines

When you intentionally change the UI:

```bash
# Re-capture baselines
uv run pytest tests/ -k visual --update-baselines

# Or manually copy current → baseline
cp baselines/current/list_page.png baselines/baseline/list_page.png
```

## Complete Visual Test Example

```python
import pytest
from chuck import ChuckClient
from chuck.screenshots import BaselineStore
from chuck.visual_diff import compare_ssim

BASELINES = BaselineStore("baselines")

@pytest.fixture
async def chuck():
    async with ChuckClient(port=1408) as client:
        yield client

async def test_list_page_visual(chuck: ChuckClient):
    """Verify the list page hasn't regressed visually."""
    await chuck.wait_for_property("stack", "busy", False)
    current = await chuck.screenshot()

    if not BASELINES.has_baseline("list_page"):
        BASELINES.save_baseline("list_page", current)
        pytest.skip("Baseline created — re-run to verify")

    baseline = BASELINES.load_baseline("list_page")
    score = compare_ssim(baseline, current)
    if score < 0.95:
        from chuck.visual_diff import diff_image
        BASELINES.save_current("list_page", current)
        BASELINES.save_diff("list_page", diff_image(baseline, current))
        pytest.fail(f"Visual regression: SSIM={score:.4f} (threshold=0.95)")
```

## Troubleshooting

| Problem | Cause | Fix |
|---------|-------|-----|
| SSIM always ~0.93 | DPI/scaling difference between machines | Use pHash instead, or normalize DPI before comparing |
| pHash distance=0 but pixels differ | Only sub-pixel rendering changes | This is correct — pHash ignores minor rendering variance |
| Baseline looks correct but test fails | Dynamic content (clock, counter) | Add mask regions for dynamic areas |
| `ModuleNotFoundError: skimage` | scikit-image not installed | `pip install scikit-image` |
| `ModuleNotFoundError: imagehash` | imagehash not installed | `pip install imagehash Pillow` |
| Diff image is all red | Completely different screenshots | Check that the app state is correct before capture |
