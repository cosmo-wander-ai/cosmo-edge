"""Deterministic RGB reference for isolated VLM evaluation.

Requires the admitted conversion environment's NumPy and Pillow. No EXIF
rotation or color-profile correction is applied. Odd padding goes right/bottom.
"""

from pathlib import Path

import numpy as np
from PIL import Image


def square_rgb(image: Image.Image, size: int = 448) -> Image.Image:
    """Pad then resize with float32 half-pixel bilinear and positive lround."""
    if size <= 0:
        raise ValueError("size must be positive")
    rgb = image.convert("RGB")
    width, height = rgb.size
    if min(width, height) <= 0:
        raise ValueError("empty image")
    side = max(width, height)
    canvas = Image.new("RGB", (side, side), (128, 128, 128))
    canvas.paste(rgb, ((side - width) // 2, (side - height) // 2))
    source = np.asarray(canvas, dtype=np.float32)
    scale = np.float32(side) / np.float32(size)
    position = (np.arange(size, dtype=np.float32) + np.float32(0.5)) * scale - np.float32(0.5)
    lower = np.floor(position).astype(np.int64)
    fraction = position - lower.astype(np.float32)
    fraction[lower < 0] = 0
    lower = np.maximum(lower, 0)
    upper = np.minimum(lower + 1, side - 1)
    fx, fy = fraction[None, :, None], fraction[:, None, None]
    one = np.float32(1)
    w00, w01 = (one - fx) * (one - fy), fx * (one - fy)
    w10, w11 = (one - fx) * fy, fx * fy
    values = w00 * source[lower[:, None], lower[None, :]]
    values = values + w01 * source[lower[:, None], upper[None, :]]
    values = values + w10 * source[upper[:, None], lower[None, :]]
    values = values + w11 * source[upper[:, None], upper[None, :]]
    return Image.fromarray(np.floor(values + np.float32(0.5)).clip(0, 255).astype(np.uint8), "RGB")


def normalized_chw(image: Image.Image, size: int = 448) -> np.ndarray:
    """Return contiguous float32 CHW, rescaled and normalized exactly once."""
    pixels = np.asarray(square_rgb(image, size), dtype=np.float32)
    pixels = pixels * (np.float32(1) / np.float32(127.5)) - np.float32(1)
    return np.ascontiguousarray(pixels.transpose(2, 0, 1))


def load_rgb(path: str | Path) -> Image.Image:
    """Decode the first image frame into detached RGB pixels."""
    with Image.open(path) as image:
        return image.convert("RGB")


def messages(prompt: str) -> list[dict]:
    """Build fresh, label-free input; the image is supplied separately."""
    if not prompt.strip():
        raise ValueError("prompt must not be empty")
    return [{"role": "user", "content": [
        {"type": "image"}, {"type": "text", "text": prompt}
    ]}]


def processor_inputs(processor, image: Image.Image, prompt: str):
    """Expand image tokens after chat templating, with thinking disabled."""
    text = processor.apply_chat_template(
        messages(prompt), tokenize=False, add_generation_prompt=True,
        enable_thinking=False,
    )
    batch = processor(
        images=[square_rgb(image)], text=[text], return_tensors="pt",
        do_resize=False,
    )
    return text, batch
