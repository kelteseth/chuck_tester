"""Image comparison utilities: SSIM, perceptual hash, and diff generation."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

from PIL import Image, ImageChops, ImageFilter


@dataclass
class CompareResult:
    """Result of comparing two images."""

    match: bool
    score: float  # 0.0–1.0 similarity
    method: str
    diff_image: Image.Image | None = field(default=None, repr=False)
    message: str = ""


def ssim_score(img_a: Image.Image, img_b: Image.Image) -> float:
    """Compute a simplified structural similarity index (0.0–1.0).

    This uses luminance, contrast, and structure comparison on grayscale
    images.  Not identical to the full Wang et al. SSIM but good enough
    for UI screenshot regression without a NumPy/SciPy dependency.
    """
    a = img_a.convert("L")
    b = img_b.convert("L")

    if a.size != b.size:
        return 0.0

    pixels_a = list(a.getdata())
    pixels_b = list(b.getdata())
    n = len(pixels_a)
    if n == 0:
        return 1.0

    mean_a = sum(pixels_a) / n
    mean_b = sum(pixels_b) / n

    var_a = sum((p - mean_a) ** 2 for p in pixels_a) / n
    var_b = sum((p - mean_b) ** 2 for p in pixels_b) / n
    cov = sum((pa - mean_a) * (pb - mean_b) for pa, pb in zip(pixels_a, pixels_b)) / n

    C1 = (0.01 * 255) ** 2
    C2 = (0.03 * 255) ** 2

    luminance = (2 * mean_a * mean_b + C1) / (mean_a**2 + mean_b**2 + C1)
    contrast = (2 * (var_a * var_b) ** 0.5 + C2) / (var_a + var_b + C2)
    structure = (cov + C2 / 2) / ((var_a * var_b) ** 0.5 + C2 / 2)

    return luminance * contrast * structure


def phash(img: Image.Image, hash_size: int = 8) -> int:
    """Compute a perceptual hash (pHash) for an image.

    Returns an integer whose Hamming distance to another pHash indicates
    visual similarity.
    """
    resized = img.convert("L").resize(
        (hash_size * 4, hash_size * 4), Image.Resampling.LANCZOS
    )
    # Apply a simple DCT-like approach: resize to hash_size to get low freqs
    small = resized.resize((hash_size, hash_size), Image.Resampling.LANCZOS)
    pixels = list(small.getdata())
    mean_val = sum(pixels) / len(pixels)
    bits = 0
    for p in pixels:
        bits = (bits << 1) | (1 if p >= mean_val else 0)
    return bits


def hamming_distance(h1: int, h2: int) -> int:
    """Count differing bits between two hashes."""
    return bin(h1 ^ h2).count("1")


def phash_score(img_a: Image.Image, img_b: Image.Image, hash_size: int = 8) -> float:
    """Perceptual hash similarity score (0.0–1.0)."""
    h1 = phash(img_a, hash_size)
    h2 = phash(img_b, hash_size)
    total_bits = hash_size * hash_size
    dist = hamming_distance(h1, h2)
    return 1.0 - dist / total_bits


def generate_diff(
    img_a: Image.Image,
    img_b: Image.Image,
    *,
    amplify: int = 10,
) -> Image.Image:
    """Generate a diff image highlighting changed regions.

    Returns an image where unchanged pixels are dimmed and changed pixels
    are highlighted in red.
    """
    a = img_a.convert("RGB")
    b = img_b.convert("RGB")

    # Pad smaller image if sizes differ
    w = max(a.width, b.width)
    h = max(a.height, b.height)
    if a.size != (w, h):
        padded = Image.new("RGB", (w, h), (0, 0, 0))
        padded.paste(a, (0, 0))
        a = padded
    if b.size != (w, h):
        padded = Image.new("RGB", (w, h), (0, 0, 0))
        padded.paste(b, (0, 0))
        b = padded

    diff = ImageChops.difference(a, b)
    # Create a mask of changed pixels (any channel changed)
    gray_diff = diff.convert("L")
    # Slight blur to group nearby changes
    blurred = gray_diff.filter(ImageFilter.GaussianBlur(radius=2))
    # Threshold
    mask = blurred.point(lambda x: 255 if x > 5 else 0)

    # Build output: dimmed baseline + red overlay on changes
    dimmed = Image.blend(a, Image.new("RGB", (w, h), (0, 0, 0)), 0.7)
    red_overlay = Image.new("RGB", (w, h), (255, 40, 40))
    result = Image.composite(red_overlay, dimmed, mask)
    return result


def apply_mask(
    img: Image.Image,
    regions: list[tuple[int, int, int, int]],
) -> Image.Image:
    """Zero out regions so they are ignored during comparison.

    Each region is (x, y, width, height).
    """
    masked = img.copy()
    for x, y, w, h in regions:
        masked.paste(Image.new(img.mode, (w, h), 0), (x, y))
    return masked


def compare(
    actual: Image.Image,
    baseline: Image.Image,
    *,
    method: str = "ssim",
    threshold: float = 0.95,
    mask_regions: list[tuple[int, int, int, int]] | None = None,
) -> CompareResult:
    """Compare two images using the specified method.

    Args:
        actual: The current screenshot.
        baseline: The stored baseline image.
        method: One of 'ssim', 'phash', 'exact'.
        threshold: Minimum similarity score to consider a match (0.0–1.0).
        mask_regions: Regions to ignore, each as (x, y, width, height).

    Returns:
        CompareResult with match status, score, and optional diff image.
    """
    a = actual
    b = baseline

    if mask_regions:
        a = apply_mask(a, mask_regions)
        b = apply_mask(b, mask_regions)

    if method == "exact":
        is_match = list(a.convert("RGB").getdata()) == list(b.convert("RGB").getdata())
        score = 1.0 if is_match else 0.0
    elif method == "phash":
        score = phash_score(a, b)
        is_match = score >= threshold
    else:  # ssim (default)
        score = ssim_score(a, b)
        is_match = score >= threshold

    diff_img = None
    if not is_match:
        diff_img = generate_diff(actual, baseline)

    msg = ""
    if not is_match:
        msg = f"{method} score {score:.4f} below threshold {threshold:.4f}"

    return CompareResult(
        match=is_match,
        score=score,
        method=method,
        diff_image=diff_img,
        message=msg,
    )
