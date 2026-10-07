"""Versioned dynamic-question inputs, separate from the frozen v1 worker.

Only padding-query rows with no visible local key receive global valid keys.
All real-query rows and all key-padding masks remain unchanged. The altered
padding rows are never used as keys or scored markers; reference/device parity
must be checked before admitting this mask policy to a deployed candidate.
"""
import copy

import numpy as np

from image_frontend import PROFILE, decode_rgb, square_image, patchify_square, decision_feeds
from question_compiler import (QTYPES, SPECIAL_IDS, fingerprint, normalize_question,
                               select_image_temperature)


def dynamic_masks(valid_length):
    length = PROFILE["sequence_length"]
    if type(valid_length) is not int or not 1 <= valid_length <= length:
        raise ValueError("invalid sequence length")
    positions = np.arange(length)
    keys = positions < valid_length
    full = np.broadcast_to(keys[None, :], (length, length))
    local = full & (np.abs(positions[:, None] - positions[None, :]) <= PROFILE["half_window"])
    empty_rows = np.flatnonzero(~local.any(axis=-1))
    if (empty_rows < valid_length).any():
        raise ValueError("real query has no visible key")
    local[empty_rows] = full[empty_rows]
    return {name: np.where(mask, 0.0, PROFILE["mask_value"]).astype(np.float32)[None, None]
            for name, mask in (("full_attention_mask", full), ("sliding_attention_mask", local))}


class DynamicImageFrontend:
    def __init__(self, pack, expected_sha256, expected_bindings, config):
        p = copy.deepcopy(pack)
        claimed = p.pop("compiled_sha256", None)
        if claimed != expected_sha256 or fingerprint(p) != expected_sha256:
            raise ValueError("compiled question identity mismatch")
        if p.get("schema") != 2 or p.get("profile") != PROFILE:
            raise ValueError("dynamic profile mismatch")
        if p.get("bindings") != expected_bindings:
            raise ValueError("question/model asset mismatch")
        if p.get("mask_policy") != "valid-keys-padded-query-fill-v1" or p.get("business_qualified") is not False:
            raise ValueError("unsupported policy or qualification claim")
        q, labels, rendered = normalize_question(p["question"])
        if (p["question_id"], p["question_version"], p["qtype"]) != (q["id"], q["version"], QTYPES[q["type"]]):
            raise ValueError("question metadata mismatch")
        if p["option_labels"] != labels or p["rendered_options"] != rendered:
            raise ValueError("question option order mismatch")
        if p["temperature"] != select_image_temperature(config, p["qtype"], len(labels)):
            raise ValueError("question temperature mismatch")
        seq = p["sequence"]
        ids, markers = seq["input_ids"], seq["marker_positions"]
        valid, pos = seq["valid_length"], seq["image_position"]
        if (not isinstance(ids, list) or not ids or
                any(type(i) is not int or not 0 <= i < PROFILE["vocabulary_size"] for i in ids)):
            raise ValueError("invalid token ids")
        if type(valid) is not int or valid != len(ids) or not 1 <= valid <= PROFILE["sequence_length"]:
            raise ValueError("sequence length mismatch")
        if type(pos) is not int or not 1 <= pos or pos + PROFILE["image_slots"] >= valid:
            raise ValueError("image slots must fit before final SEP")
        if ids[0] != SPECIAL_IDS["cls"] or ids[-1] != SPECIAL_IDS["sep"]:
            raise ValueError("sequence boundary mismatch")
        if ids[pos:pos + PROFILE["image_slots"]] != [SPECIAL_IDS["pad"]] * PROFILE["image_slots"]:
            raise ValueError("image placeholders mismatch")
        if (not isinstance(markers, list) or len(markers) != len(labels) or
                any(type(m) is not int or not 1 <= m < pos or ids[m] != SPECIAL_IDS["mask"] for m in markers) or
                any(a >= b for a, b in zip(markers, markers[1:]))):
            raise ValueError("question marker mismatch")
        self.pack = p
        self.sha256 = expected_sha256
        self.masks = dynamic_masks(valid)

    def prepare(self, image, square_policy="center-pad-white-v1"):
        p = self.pack
        img, metadata = decode_rgb(image)
        img, transform = square_image(img, square_policy)
        metadata.update(transform)
        metadata.update({"question_id": p["question_id"], "question_version": p["question_version"],
                         "compiled_question_sha256": self.sha256, "profile": PROFILE["id"],
                         "mask_policy": p["mask_policy"], "business_qualified": False})
        s = p["sequence"]
        return {"pixel_values": patchify_square(img),
                "input_ids": np.asarray(s["input_ids"], dtype=np.int64)[None],
                "valid_length": s["valid_length"], "image_position": s["image_position"],
                "marker_positions": np.asarray(s["marker_positions"], dtype=np.int64)[None],
                "marker_mask": np.ones((1, len(p["option_labels"])), dtype=bool),
                "qtype": p["qtype"], "option_labels": list(p["option_labels"]),
                "temperature": dict(p["temperature"]), "metadata": metadata,
                **{key: value.copy() for key, value in self.masks.items()}}


def dynamic_decision_feeds(prepared, visual_tokens, table, heads):
    # Preserve the already measured embedding splice and shape checks.
    result = decision_feeds(prepared, visual_tokens, table, heads)
    qtype = prepared["qtype"]
    if type(qtype) is not int or qtype not in QTYPES.values():
        raise ValueError("unsupported dynamic question type")
    result["type_embedding"] = heads["type_emb.weight"][[qtype]][:, None].astype(np.float32)
    return result
