"""Baseline screenshot management: save, load, organize, and update."""

from __future__ import annotations

import base64
from pathlib import Path

from PIL import Image

from .visual_diff import CompareResult, compare, generate_diff

# Default baseline directory (relative to session file or cwd)
DEFAULT_BASELINE_DIR = "baselines"


class BaselineStore:
    """Manages a directory of baseline PNG screenshots.

    Structure::

        baselines/
            initial_state.png
            after_login.png
            final_state.png
            diffs/
                initial_state.diff.png
                after_login.diff.png
    """

    def __init__(self, baseline_dir: str | Path = DEFAULT_BASELINE_DIR) -> None:
        self._dir = Path(baseline_dir)

    @property
    def dir(self) -> Path:
        return self._dir

    def ensure_dir(self) -> None:
        self._dir.mkdir(parents=True, exist_ok=True)
        (self._dir / "diffs").mkdir(exist_ok=True)

    def baseline_path(self, tag: str) -> Path:
        return self._dir / f"{tag}.png"

    def diff_path(self, tag: str) -> Path:
        return self._dir / "diffs" / f"{tag}.diff.png"

    def actual_path(self, tag: str) -> Path:
        return self._dir / "diffs" / f"{tag}.actual.png"

    def has_baseline(self, tag: str) -> bool:
        return self.baseline_path(tag).is_file()

    def load_baseline(self, tag: str) -> Image.Image:
        path = self.baseline_path(tag)
        if not path.is_file():
            raise FileNotFoundError(f"No baseline for tag '{tag}' at {path}")
        return Image.open(path)

    def save_baseline(self, tag: str, image: Image.Image) -> Path:
        self.ensure_dir()
        path = self.baseline_path(tag)
        image.save(path, "PNG")
        return path

    def save_from_base64(self, tag: str, data: str) -> Path:
        self.ensure_dir()
        path = self.baseline_path(tag)
        path.write_bytes(base64.b64decode(data))
        return path

    def save_diff(self, tag: str, diff_image: Image.Image) -> Path:
        self.ensure_dir()
        path = self.diff_path(tag)
        diff_image.save(path, "PNG")
        return path

    def save_actual(self, tag: str, image: Image.Image) -> Path:
        self.ensure_dir()
        path = self.actual_path(tag)
        image.save(path, "PNG")
        return path

    def list_baselines(self) -> list[str]:
        if not self._dir.is_dir():
            return []
        return sorted(p.stem for p in self._dir.glob("*.png"))

    def verify(
        self,
        tag: str,
        actual: Image.Image,
        *,
        method: str = "ssim",
        threshold: float = 0.95,
        mask_regions: list[tuple[int, int, int, int]] | None = None,
    ) -> CompareResult:
        """Compare *actual* screenshot against the stored baseline.

        On mismatch, saves the actual image and the diff image alongside
        the baseline for inspection.
        """
        baseline = self.load_baseline(tag)
        result = compare(
            actual,
            baseline,
            method=method,
            threshold=threshold,
            mask_regions=mask_regions,
        )

        if not result.match:
            self.save_actual(tag, actual)
            if result.diff_image:
                self.save_diff(tag, result.diff_image)

        return result

    def update_baseline(self, tag: str, actual: Image.Image) -> Path:
        """Replace the baseline for *tag* with the given image."""
        # Clean up any old diff artifacts
        for p in (self.diff_path(tag), self.actual_path(tag)):
            p.unlink(missing_ok=True)
        return self.save_baseline(tag, actual)
