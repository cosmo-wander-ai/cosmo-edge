"""Private fixed-profile Laya-V image frontend. No runtime tokenizer or network.

Only the externally hash-bound question pack is supported. Pixel values are
computed from each supplied image. White square padding is an explicit system
adapter, not claimed equivalent to upstream arbitrary-aspect NaFlex inference.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path

import numpy as np
from PIL import Image

MAX_BYTES = 32 * 1024 * 1024
MAX_PIXELS = 64_000_000
PROFILE = {
    "id": "laya-256p-256s-v1", "grid": [16, 16], "patch_size": 16,
    "patch_budget": 256, "sequence_length": 256, "hidden_size": 768,
    "image_slots": 130, "half_window": 64, "mask_value": -10000.0,
    "vocabulary_size": 256000,
}


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def additive_masks(valid_length):
    """Include every padded query in the finite-mask visibility check."""
    if type(valid_length) is not int or not 1 <= valid_length <= 256:
        raise ValueError("valid_length must be an integer in [1,256]")
    indices = np.arange(256)
    visible = np.broadcast_to(indices[None, :] < valid_length, (256, 256))
    local = visible & (np.abs(indices[:, None] - indices[None, :]) <= 64)
    result = {}
    for name, mask in (("full_attention_mask", visible), ("sliding_attention_mask", local)):
        if not mask.any(axis=-1).all():
            raise ValueError("finite mask rejected: a query row has no visible key")
        result[name] = np.where(mask, 0.0, -10000.0).astype(np.float32)[None, None]
    return result


def decode_rgb(value):
    """Match source RGB/alpha handling; deliberately do not apply EXIF rotation."""
    source_hash = None
    if isinstance(value, (str, Path)):
        path = Path(value)
        if path.stat().st_size > MAX_BYTES:
            raise ValueError("image exceeds encoded byte limit")
        return decode_rgb(path.read_bytes())
    if isinstance(value, (bytes, bytearray, memoryview)):
        raw = bytes(value)
        if not raw or len(raw) > MAX_BYTES:
            raise ValueError("image bytes are empty or exceed limit")
        source_hash = hashlib.sha256(raw).hexdigest()
        img = Image.open(io.BytesIO(raw))
    elif isinstance(value, Image.Image):
        img = value
    else:
        raise TypeError("image must be encoded bytes, a local path, or PIL Image")
    if getattr(img, "n_frames", 1) != 1:
        raise ValueError("multi-frame images are outside this static profile")
    if min(img.size) < 1 or img.width * img.height > MAX_PIXELS:
        raise ValueError("image exceeds decoded pixel limit")
    img.load()
    original_mode = img.mode
    if img.mode != "RGB":
        if img.mode in ("RGBA", "LA", "P"):
            rgba = img.convert("RGBA")
            img = Image.new("RGB", rgba.size, (255, 255, 255))
            img.paste(rgba, mask=rgba.split()[-1])
        else:
            img = img.convert("RGB")
    return img, {"encoded_image_sha256": source_hash, "original_size": list(img.size),
                 "original_mode": original_mode, "exif_transpose": False}


def square_image(img, policy):
    if policy not in ("square-only", "center-pad-white-v1"):
        raise ValueError("unsupported square policy")
    w, h = img.size
    side = max(w, h)
    # Upstream _target_size caps scale at 100, so 1/2-pixel square inputs
    # cannot fill its 16x16 patch grid. Do not silently force a different grid.
    if side < 3:
        raise ValueError("image is too small for the upstream 16x16 grid")
    if side * side > MAX_PIXELS:
        raise ValueError("square canvas exceeds pixel limit")
    left, top = (side - w) // 2, (side - h) // 2
    pads = [left, top, side - w - left, side - h - top]
    if w != h:
        if policy == "square-only":
            raise ValueError("current compiled profile requires a square image")
        canvas = Image.new("RGB", (side, side), (255, 255, 255))
        canvas.paste(img, (left, top))
        img = canvas
    return img, {"square_policy": policy, "padding_ltrb": pads,
                 "square_size": [side, side], "padding_rgb": [255, 255, 255]}


def patchify_square(img):
    if img.mode != "RGB" or img.width != img.height or img.width < 3:
        raise ValueError("patchify_square needs a square RGB image")
    # Same arithmetic order, interpolation, channels, and raster patch order
    # as upstream vision.patchify for square input / budget 256 / patch 16.
    bilinear = Image.Resampling.BILINEAR if hasattr(Image, "Resampling") else Image.BILINEAR
    arr = np.asarray(img.resize((256, 256), bilinear), dtype=np.float32)
    arr = (arr / 255.0 - 0.5) / 0.5
    patches = arr.reshape(16, 16, 16, 16, 3).transpose(0, 2, 1, 3, 4)
    return np.ascontiguousarray(patches.reshape(1, 256, 768))


class ImageFrontend:
    def __init__(self, pack_path, expected_pack_sha256):
        if not isinstance(expected_pack_sha256, str) or len(expected_pack_sha256) != 64:
            raise ValueError("an externally frozen question pack SHA256 is required")
        if sha256(pack_path) != expected_pack_sha256:
            raise ValueError("question pack identity mismatch")
        self.pack_sha256 = expected_pack_sha256
        self.pack = json.loads(Path(pack_path).read_text())
        p = self.pack
        if p.get("schema") != 1 or p.get("profile") != PROFILE:
            raise ValueError("question pack profile mismatch")
        if p.get("question_id") != "helmet-review-en-v1" or p.get("question_version") != 1:
            raise ValueError("only helmet-review-en-v1/version 1 is supported")
        if p.get("text_state") != "" or p.get("qtype") != 0:
            raise ValueError("only empty text-state choice question is supported")
        self.labels = p["option_labels"]
        if self.labels != ["wearing", "not_wearing", "uncertain"]:
            raise ValueError("frozen option order mismatch")
        if list(p["question"]["criteria"]) != self.labels:
            raise ValueError("question criteria order mismatch")
        if p["question"]["type"] != "choice":
            raise ValueError("unsupported question type")
        s = p["sequence"]
        for key in ("input_ids", "marker_positions"):
            if not isinstance(s[key], list) or any(type(v) is not int for v in s[key]):
                raise ValueError("sequence values must be integer lists")
        self.ids = np.asarray(s["input_ids"], dtype=np.int64)[None]
        self.markers = np.asarray(s["marker_positions"], dtype=np.int64)[None]
        self.valid_length = self.ids.shape[1]
        self.image_position = s["image_position"]
        if type(self.image_position) is not int or s["valid_length"] != self.valid_length:
            raise ValueError("invalid sequence length or image position")
        self.masks = additive_masks(self.valid_length)
        if self.ids.min() < 0 or self.ids.max() >= 256000:
            raise ValueError("token id outside vocabulary")
        if self.ids[0, 0] != 2 or self.ids[0, -1] != 1:
            raise ValueError("CLS/SEP convention mismatch")
        if not 1 <= self.image_position or self.image_position + 130 != self.valid_length - 1:
            raise ValueError("image slots must precede final SEP without text state")
        if not np.all(self.ids[0, self.image_position:self.image_position + 130] == 0):
            raise ValueError("image slots must be pad placeholders")
        if self.markers.shape != (1, 3) or not np.all(np.diff(self.markers[0]) > 0):
            raise ValueError("expected three ordered markers")
        if self.markers.min() < 1 or self.markers.max() >= self.image_position:
            raise ValueError("markers must be inside the question prefix")
        if not np.all(self.ids[0, self.markers[0]] == 4):
            raise ValueError("marker token mismatch")
        if p["temperature"] != {"bucket": "image|choice:3-5", "value": 1.1040229797363281}:
            raise ValueError("temperature identity mismatch")

    def prepare(self, image, square_policy="square-only"):
        img, meta = decode_rgb(image)
        img, transform = square_image(img, square_policy)
        pixels = patchify_square(img)
        meta.update(transform)
        meta.update({"question_id": self.pack["question_id"], "question_version": 1,
                     "question_pack_sha256": self.pack_sha256, "profile": PROFILE["id"],
                     "patch_grid": [16, 16], "valid_length": self.valid_length,
                     "pixel_values_sha256": hashlib.sha256(pixels.tobytes()).hexdigest(),
                     "aspect_scope": "upstream parity applies to the square-normalized image"})
        return {"pixel_values": pixels, "input_ids": self.ids.copy(),
                "valid_length": self.valid_length, "image_position": self.image_position,
                "marker_positions": self.markers.copy(), "marker_mask": np.ones((1, 3), dtype=bool),
                "qtype": 0, "option_labels": list(self.labels), "metadata": meta,
                **{key: value.copy() for key, value in self.masks.items()}}


def load_cpu_assets(cpu_directory, pack):
    """Verify once at worker startup; retain mmap table and small copied heads."""
    directory = Path(cpu_directory)
    for key in ("token_embeddings.f16.npy", "cpu_heads.npz", "rl_agent_config.json"):
        if sha256(directory / key) != pack["cpu_files"][key]:
            raise ValueError("CPU asset identity mismatch: " + key)
    table = np.load(directory / "token_embeddings.f16.npy", mmap_mode="r", allow_pickle=False)
    if table.shape != (256000, 768) or table.dtype != np.float16:
        raise ValueError("CPU embedding table profile mismatch")
    with np.load(directory / "cpu_heads.npz", allow_pickle=False) as data:
        heads = {key: data[key].copy() for key in data.files}
    cfg = json.loads((directory / "rl_agent_config.json").read_text())
    if cfg["temperature_by_options"]["image|choice:3-5"] != pack["temperature"]["value"]:
        raise ValueError("CPU model temperature mismatch")
    return table, heads, cfg


def decision_feeds(prepared, visual_tokens, table, heads):
    """Fresh adapter output replaces all 130 image slots for this request."""
    visual = np.asarray(visual_tokens)
    if visual.shape != (1, 130, 768) or visual.dtype != np.float32 or not np.isfinite(visual).all():
        raise ValueError("visual_tokens must be finite F32[1,130,768]")
    if table.shape != (256000, 768) or table.dtype != np.float16:
        raise ValueError("unexpected CPU table")
    ids = np.zeros(256, dtype=np.int64)
    valid, image_pos = prepared["valid_length"], prepared["image_position"]
    if prepared["input_ids"].shape != (1, valid) or image_pos + 130 > valid:
        raise ValueError("prepared sequence contract mismatch")
    ids[:valid] = prepared["input_ids"][0]
    if ids.min() < 0 or ids.max() >= 256000:
        raise ValueError("invalid token id")
    emb = np.asarray(table[ids], dtype=np.float32)[None].copy()
    emb[:, image_pos:image_pos + 130] = visual
    type_weights = heads["type_emb.weight"]
    if type_weights.shape != (3, 768) or not np.isfinite(type_weights).all():
        raise ValueError("type embedding contract mismatch")
    return {"inputs_embeds": emb, "type_embedding": type_weights[[0]][:, None].astype(np.float32),
            "full_attention_mask": prepared["full_attention_mask"],
            "sliding_attention_mask": prepared["sliding_attention_mask"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pack", type=Path, required=True)
    parser.add_argument("--pack-sha256", required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--square-policy", choices=("square-only", "center-pad-white-v1"), default="square-only")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(mode=0o700, parents=True, exist_ok=False)
    prepared = ImageFrontend(args.pack, args.pack_sha256).prepare(args.image, args.square_policy)
    arrays = {key: value for key, value in prepared.items() if isinstance(value, np.ndarray)}
    np.savez(args.output / "frontend-inputs.npz", **arrays)
    meta = {key: value for key, value in prepared.items() if not isinstance(value, np.ndarray)}
    (args.output / "frontend.json").write_text(json.dumps(meta, indent=2, ensure_ascii=False) + "\n")
    print(json.dumps({"status": "PREPARED", "metadata": meta}, ensure_ascii=False))


if __name__ == "__main__":
    main()
