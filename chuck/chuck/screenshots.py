"""Baseline screenshot management: save, load, organize, and update."""

from __future__ import annotations

import base64
import io
from pathlib import Path

from PIL import Image

from .visual_diff import CompareResult, compare, generate_diff

# Default baseline directory (relative to session file or cwd)
DEFAULT_BASELINE_DIR = "baselines"

# AVIF baselines are about a quarter of the PNG size. They are lossy, so the
# "exact" compare method needs PNG; at this quality SSIM moves by under 0.001.
_SAVE_OPTIONS = {
    "png": ("PNG", {}),
    "avif": ("AVIF", {"quality": 90, "subsampling": "4:4:4"}),
}


class BaselineStore:
    """Manages a directory of baseline screenshots, as PNG or AVIF files.

    Structure::

        baselines/
            initial_state.png
            after_login.png
            final_state.png
            diffs/
                initial_state.diff.png
                after_login.diff.png
    """

    def __init__(self, baseline_dir: str | Path = DEFAULT_BASELINE_DIR, *, format: str = "png") -> None:
        if format not in _SAVE_OPTIONS:
            raise ValueError(f"Unknown baseline format '{format}', expected one of {sorted(_SAVE_OPTIONS)}")
        self._dir = Path(baseline_dir)
        self._format = format

    @property
    def dir(self) -> Path:
        return self._dir

    def ensure_dir(self) -> None:
        self._dir.mkdir(parents=True, exist_ok=True)
        (self._dir / "diffs").mkdir(exist_ok=True)

    def baseline_path(self, tag: str) -> Path:
        return self._dir / f"{tag}.{self._format}"

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
        name, options = _SAVE_OPTIONS[self._format]
        image.save(path, name, **options)
        return path

    def save_from_base64(self, tag: str, data: str) -> Path:
        return self.save_baseline(tag, Image.open(io.BytesIO(base64.b64decode(data))))

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
        return sorted(p.stem for p in self._dir.glob(f"*.{self._format}"))

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
