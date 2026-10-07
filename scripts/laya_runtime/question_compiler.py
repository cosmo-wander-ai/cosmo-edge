"""Offline, bounded question preparation for the Laya visual-decision backend.

This module prepares inputs only. It does not qualify a scene, select an alarm
policy, download assets, or change the existing frozen helmet worker protocol.
The sequence layout follows the pinned Laya build_sequence implementation.
Unlike that implementation, configuration overflow is an error, never a silent
truncation. Option order is part of the compiled identity.
"""
import copy
import hashlib
import json
import math
from pathlib import Path
import re
import threading

from image_frontend import PROFILE, sha256

QTYPES = {"choice": 0, "noul": 2}
MAX_OPTIONS = 16
MAX_TEXT_BYTES = 16384
SPECIAL_IDS = {"cls": 2, "sep": 1, "pad": 0, "mask": 4}


def canonical_bytes(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":"), allow_nan=False).encode("utf-8")


def fingerprint(value):
    return hashlib.sha256(canonical_bytes(value)).hexdigest()


def _text(value, field, empty=False):
    if not isinstance(value, str) or (not empty and not value.strip()):
        raise ValueError(field + ": nonempty text required")
    if len(value.encode("utf-8")) > MAX_TEXT_BYTES:
        raise ValueError(field + ": text byte limit exceeded")
    if "<mask>" in value:
        # Reject rather than silently replacing a marker in the user's question.
        raise ValueError(field + ": reserved marker is not allowed")
    return value


def normalize_question(question):
    if not isinstance(question, dict):
        raise ValueError("question must be an object")
    unknown = set(question) - {"id", "version", "type", "instructions", "criteria", "labels"}
    if unknown:
        raise ValueError("unsupported question fields: " + ",".join(sorted(unknown)))
    q = copy.deepcopy(question)
    if not isinstance(q.get("id"), str) or not re.fullmatch(r"[A-Za-z0-9_.:-]{1,96}", q["id"]):
        raise ValueError("invalid question id")
    if type(q.get("version")) is not int or not 1 <= q["version"] <= 2147483647:
        raise ValueError("positive integer question version required")
    if q.get("type") not in QTYPES:
        raise ValueError("supported question types are choice and noul")
    _text(q.get("instructions"), "instructions")
    criteria = q.get("criteria")
    if q["type"] == "choice":
        if "labels" in q or not isinstance(criteria, dict) or not 2 <= len(criteria) <= MAX_OPTIONS:
            raise ValueError("choice requires 2..16 ordered criteria and no labels override")
        labels, options = [], []
        for label, description in criteria.items():
            _text(label, "option label")
            if len(json.dumps(label, ensure_ascii=False).encode("utf-8")) > 256:
                raise ValueError("option label byte limit exceeded")
            if description is not None:
                _text(description, "option description", empty=True)
            labels.append(label)
            options.append(label if description in (None, "") else label + ": " + description)
    else:
        if criteria is None:
            criteria = {}
        if not isinstance(criteria, dict) or set(criteria) - {"false", "true"}:
            raise ValueError("noul criteria use only false/true keys")
        override = q.get("labels", {"false": "false", "true": "true"})
        if not isinstance(override, dict) or set(override) != {"false", "true"}:
            raise ValueError("noul labels must contain false and true")
        rendered = [_text(override[k], "noul label").strip() for k in ("false", "true")]
        if rendered[0] == rendered[1]:
            raise ValueError("noul labels must be distinct")
        labels = ["false", "true"]  # Semantic order never follows dictionary insertion order.
        defaults = ["no, the statement does not hold", "yes, the statement holds"]
        options = []
        for key, label, default in zip(labels, rendered, defaults):
            description = criteria.get(key)
            if description is not None:
                _text(description, "noul criterion", empty=True)
            options.append(label + ": " + (default if description in (None, "") else description))
    return q, labels, options


def select_image_temperature(config, qtype, count):
    name = next(name for name, value in QTYPES.items() if value == qtype)
    size = "2" if count <= 2 else "3-5" if count <= 5 else "6-10" if count <= 10 else "11+"
    bucket = "image|" + name + ":" + size
    values = config.get("temperature_by_options", {})
    if bucket in values:
        raw = values[bucket]
        source = bucket
    elif config.get("temperature_image"):
        raw = config["temperature_image"][qtype]
        source = "temperature_image"
    else:
        raw = values.get(name + ":" + size, config["temperature"][qtype])
        source = "text_fallback"
    value = float(raw)
    # The upstream clamp is applied when loading the model configuration.
    if not math.isfinite(value):
        value = 1.0
    value = min(5.0, max(0.5, value))
    return {"bucket": bucket, "source": source, "value": value}


class FrozenTokenizer:
    """A local tokenizers-only frontend; PyTorch/Transformers are not required."""

    def __init__(self, path, expected_sha256):
        self.path = Path(path)
        if sha256(self.path) != expected_sha256:
            raise ValueError("tokenizer identity mismatch")
        from tokenizers import Tokenizer
        self._tokenizer = Tokenizer.from_file(str(self.path))
        self._tokenizer.no_truncation()
        self._tokenizer.no_padding()
        self._lock = threading.RLock()
        self.sha256 = expected_sha256
        for name, token in (("cls", "<bos>"), ("sep", "<eos>"), ("pad", "<pad>"), ("mask", "<mask>")):
            if self._tokenizer.token_to_id(token) != SPECIAL_IDS[name]:
                raise ValueError("tokenizer special-token mismatch: " + name)

    def encode(self, text):
        with self._lock:
            return self._tokenizer.encode(text, add_special_tokens=False).ids


class QuestionCompiler:
    def __init__(self, tokenizer, config, model_config_sha256):
        self.tokenizer = tokenizer
        self.config = copy.deepcopy(config)
        self.config_sha256 = model_config_sha256

    def compile(self, question, text_state=""):
        q, labels, options = normalize_question(question)
        _text(text_state, "text_state", empty=True)
        head = self.tokenizer.encode(q["type"] + " question: " + q["instructions"])
        option_ids = [self.tokenizer.encode(" " + option) for option in options]
        if any(len(ids) > 48 for ids in option_ids):
            raise ValueError("option_token_budget_exceeded: shorten the option description")
        if len(head) + sum(len(ids) + 1 for ids in option_ids) > self.config.get("head_max_len", 256):
            raise ValueError("question_head_budget_exceeded")
        ids = [SPECIAL_IDS["cls"]] + head + [SPECIAL_IDS["sep"]]
        markers = []
        for tokens in option_ids:
            markers.append(len(ids))
            ids.extend([SPECIAL_IDS["mask"]] + tokens)
        ids.append(SPECIAL_IDS["sep"])
        image_position = len(ids)
        ids.extend([SPECIAL_IDS["pad"]] * PROFILE["image_slots"])
        ids.extend(self.tokenizer.encode(text_state))
        ids.append(SPECIAL_IDS["sep"])
        if len(ids) > PROFILE["sequence_length"]:
            raise ValueError("sequence_budget_exceeded: required=%d available=%d" %
                             (len(ids), PROFILE["sequence_length"]))
        if any(type(i) is not int or not 0 <= i < PROFILE["vocabulary_size"] for i in ids):
            raise ValueError("token id outside vocabulary")
        pack = {"schema": 2, "profile": copy.deepcopy(PROFILE),
                "question_id": q["id"], "question_version": q["version"],
                "question": q, "text_state": text_state, "qtype": QTYPES[q["type"]],
                "option_labels": labels, "rendered_options": options,
                "bindings": {"tokenizer_sha256": self.tokenizer.sha256,
                             "model_config_sha256": self.config_sha256},
                "temperature": select_image_temperature(self.config, QTYPES[q["type"]], len(labels)),
                "sequence": {"input_ids": ids, "marker_positions": markers,
                             "image_position": image_position, "valid_length": len(ids)},
                "mask_policy": "valid-keys-padded-query-fill-v1",
                "business_qualified": False}
        pack["compiled_sha256"] = fingerprint(pack)
        return pack


class QuestionCache:
    """Content-addressed cache; readers must request the exact saved revision."""

    def __init__(self, directory):
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=True, mode=0o700)

    def put(self, pack):
        body = copy.deepcopy(pack)
        key = body.pop("compiled_sha256")
        if key != fingerprint(body):
            raise ValueError("compiled question identity mismatch")
        path = self.directory / (key + ".json")
        # Keep the user's option insertion order on disk as well as in labels.
        payload = json.dumps(pack, ensure_ascii=False, separators=(",", ":"),
                             allow_nan=False).encode("utf-8") + b"\n"
        try:
            # A private temporary file and atomic rename avoid publishing partial JSON.
            import os
            import tempfile
            fd, temporary = tempfile.mkstemp(prefix=".question-", dir=self.directory)
            try:
                with os.fdopen(fd, "wb") as stream:
                    stream.write(payload)
                    stream.flush()
                    os.fsync(stream.fileno())
                os.replace(temporary, path)
            finally:
                if os.path.exists(temporary):
                    os.unlink(temporary)
        except OSError:
            raise
        return key

    def get(self, key):
        if not isinstance(key, str) or not re.fullmatch(r"[0-9a-f]{64}", key):
            raise ValueError("invalid compiled question identity")
        pack = json.loads((self.directory / (key + ".json")).read_text())
        claimed = pack.pop("compiled_sha256")
        if claimed != key or fingerprint(pack) != key:
            raise ValueError("compiled question cache corrupted")
        pack["compiled_sha256"] = key
        return pack
