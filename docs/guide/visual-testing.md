# Visual Testing

Chuck can capture screenshots. Testa compares them against stored baselines to detect visual regressions.

## Capture a Screenshot

```python
# Full window
await app.screenshot(save_to="screenshot.png")

# Specific item
await app.screenshot("myWidget", save_to="widget.png")
```

## Baseline Management

Screenshots are compared against baselines stored in a directory:

```
baselines/
├── login_page.png          # reference screenshots
├── dashboard.png
└── diffs/
    ├── login_page.actual.png   # what we got (on mismatch)
    └── login_page.diff.png     # highlighted differences
```

### Create Baselines

The first time you run a screenshot with `verify=True` and no baseline exists, it's saved as the baseline:

```python
await app.screenshot(tag="login_page", verify=True)
# First run: saves baselines/login_page.png
# Subsequent runs: compares against it
```

Or capture all baselines from a JSONL session:

```bash
uv run testa baseline session.jsonl
```

### Update Baselines

When the UI intentionally changes:

```bash
# Re-run the session to capture new baselines
uv run testa baseline session.jsonl --baseline-dir baselines
```

Or delete the old baseline and re-run — it'll be recreated.

## Comparison Methods

### SSIM (default)

Structural Similarity Index. Tolerant of minor anti-aliasing and sub-pixel rendering differences.

```python
await app.screenshot(tag="page", verify=True, method="ssim", threshold=0.95)
```

- Score 1.0 = identical
- Score ≥ 0.95 = visually equivalent (default threshold)
- Score < 0.95 = likely a real change

### Perceptual Hash (pHash)

Fast, tolerant of color shifts and minor layout changes. Good for "is it roughly the same?"

```python
await app.screenshot(tag="page", verify=True, method="phash", threshold=0.90)
```

### Exact

Pixel-by-pixel comparison. Use only for pixel-perfect UIs or generated content.

```python
await app.screenshot(tag="icon", verify=True, method="exact")
```

## Mask Regions

Ignore dynamic content (timestamps, random IDs) during comparison:

```python
await app.screenshot(
    tag="dashboard",
    verify=True,
    mask_regions=[
        (10, 50, 200, 30),    # (x, y, width, height) — timestamp area
        (500, 100, 150, 20),  # user ID area
    ],
)
```

## CI Integration

```bash
# Fail the build on any visual regression
uv run testa verify session.jsonl --fail-on-diff --method ssim --threshold 0.95
```

Exit code 0 = all screenshots match. Exit code 1 = mismatch detected.

### Diff Inspection

On mismatch, testa saves:

- `baselines/diffs/<tag>.actual.png` — what the app looked like
- `baselines/diffs/<tag>.diff.png` — red-highlighted changed regions

Upload these as CI artifacts for review.

## Python API

```python
from testa import BaselineStore, CompareResult

store = BaselineStore("baselines")

# Manual comparison
from PIL import Image
actual = Image.open("screenshot.png")
result: CompareResult = store.verify("login_page", actual, method="ssim", threshold=0.95)
print(result.match)   # True/False
print(result.score)   # 0.0–1.0
print(result.method)  # "ssim"
```
