"""Phase 3 demo: baseline screenshots and visual diff verification."""

import asyncio
import shutil
from pathlib import Path

from chuck import ChuckClient

BASELINE_DIR = Path("test_baselines")


async def demo():
    # Clean slate
    if BASELINE_DIR.exists():
        shutil.rmtree(BASELINE_DIR)

    async with ChuckClient(port=1408, baseline_dir=BASELINE_DIR) as app:
        # --- Step 1: Take a tagged screenshot — should auto-create baseline ---
        print("=== Step 1: Create baseline ===")
        result = await app.screenshot(tag="initial_state", verify=True)
        print(f"  baseline_created: {result.get('baseline_created')}")
        print(f"  size: {result.get('width')}x{result.get('height')}")
        assert result.get("baseline_created"), "Expected baseline to be created"
        assert (BASELINE_DIR / "initial_state.png").exists()
        print("  ✓ Baseline saved\n")

        # --- Step 2: Verify unchanged state — should match ---
        print("=== Step 2: Verify (unchanged) ===")
        result = await app.screenshot(tag="initial_state", verify=True)
        print(f"  match: {result.get('match')}")
        print(f"  score: {result.get('score', 'N/A')}")
        assert result.get("match") is True, f"Expected match, got {result}"
        print("  ✓ Match confirmed\n")

        # --- Step 3: Change the UI, then verify — should mismatch ---
        print("=== Step 3: Change UI and verify (expect mismatch) ===")
        await app.keys("nameField", text="World")
        await app.click("greetBtn")
        result = await app.screenshot(tag="initial_state", verify=True)
        print(f"  match: {result.get('match')}")
        print(f"  score: {result.get('score', 'N/A'):.4f}")
        print(f"  message: {result.get('message', '')}")
        assert result.get("match") is False, f"Expected mismatch, got {result}"
        assert (BASELINE_DIR / "diffs" / "initial_state.diff.png").exists()
        assert (BASELINE_DIR / "diffs" / "initial_state.actual.png").exists()
        print("  ✓ Mismatch detected, diff images saved\n")

        # --- Step 4: Update baseline to new state ---
        print("=== Step 4: Update baseline ===")
        await app.screenshot(tag="after_greet", verify=True)
        assert (BASELINE_DIR / "after_greet.png").exists()
        print("  ✓ New baseline 'after_greet' created\n")

        # --- Step 5: Reset and verify original baseline ---
        print("=== Step 5: Reset and verify original ===")
        await app.click("resetBtn")
        result = await app.screenshot(tag="initial_state", verify=True)
        print(f"  match: {result.get('match')}")
        print(f"  score: {result.get('score', 'N/A'):.4f}")
        assert result.get("match") is True
        print("  ✓ Back to original state\n")

        # --- List baselines ---
        tags = app.baselines.list_baselines()
        print(f"Baselines stored: {tags}")

    print("\nAll Phase 3 checks passed!")


asyncio.run(demo())
