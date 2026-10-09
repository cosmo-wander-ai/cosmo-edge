"""Local typed visual-decision protocol, independent of text generation APIs."""
import json
import math
import re
import struct
import time

from laya_shadow_worker import read_exact

PROTOCOL = "cosmo-visual-decision-v1"
PROFILE = "laya-256p-256s-v1"
MAX_JSON = 64 * 1024
MAX_IMAGE = 2 * 1024 * 1024
MAX_ITEMS = 8
IDENTITIES = ("request_id", "task_id", "run_epoch", "frame_id", "roi_id", "config_revision")
SHA = re.compile(r"[0-9a-f]{64}")


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate_json_key")
        result[key] = value
    return result


def parse_json(data):
    def constant(_):
        raise ValueError("nonfinite_json")
    try:
        value = json.loads(data, object_pairs_hook=unique_object, parse_constant=constant)
    except (RecursionError, UnicodeError) as error:
        raise ValueError("invalid_json") from error
    stack = [(value, 0)]
    while stack:
        item, depth = stack.pop()
        if depth > 16:
            raise ValueError("json_too_deep")
        if isinstance(item, float) and not math.isfinite(item):
            raise ValueError("nonfinite_json")
        if isinstance(item, dict):
            stack.extend((v, depth + 1) for v in item.values())
        elif isinstance(item, list):
            stack.extend((v, depth + 1) for v in item)
    return value


def identity(value):
    return isinstance(value, str) and 1 <= len(value.encode("utf-8")) <= 256 and not any(
        ord(c) < 32 for c in value)


def validate_request(value):
    fields = set(IDENTITIES) | {"protocol", "profile", "manifest_sha256", "items",
                               "deadline_monotonic_ms", "image_encoding", "image_size",
                               "image_width", "image_height"}
    if not isinstance(value, dict) or set(value) != fields:
        raise ValueError("invalid_request_fields")
    if value["protocol"] != PROTOCOL or value["profile"] != PROFILE:
        raise ValueError("unsupported_protocol_or_profile")
    if any(not identity(value[k]) for k in IDENTITIES):
        raise ValueError("invalid_request_identity")
    if not isinstance(value["manifest_sha256"], str) or not SHA.fullmatch(value["manifest_sha256"]):
        raise ValueError("invalid_manifest_identity")
    deadline = value["deadline_monotonic_ms"]
    if type(deadline) is not int or not 0 < deadline <= int(time.monotonic() * 1000) + 30000:
        raise ValueError("invalid_deadline")
    if value["image_encoding"] != "jpeg":
        raise ValueError("unsupported_image_encoding")
    if type(value["image_size"]) is not int or not 1 <= value["image_size"] <= MAX_IMAGE:
        raise ValueError("invalid_image_size")
    for name in ("image_width", "image_height"):
        if type(value[name]) is not int or not 1 <= value[name] <= 2048:
            raise ValueError("invalid_image_dimensions")
    items = value["items"]
    if not isinstance(items, list) or not 1 <= len(items) <= MAX_ITEMS:
        raise ValueError("invalid_item_count")
    ids = set()
    for item in items:
        if not isinstance(item, dict) or set(item) != {"item_id", "question_id", "question_version", "compiled_sha256"}:
            raise ValueError("invalid_item_fields")
        if not identity(item["item_id"]) or item["item_id"] in ids or not identity(item["question_id"]):
            raise ValueError("invalid_or_duplicate_item_identity")
        ids.add(item["item_id"])
        if type(item["question_version"]) is not int or not 1 <= item["question_version"] <= 2147483647:
            raise ValueError("invalid_question_version")
        if not isinstance(item["compiled_sha256"], str) or not SHA.fullmatch(item["compiled_sha256"]):
            raise ValueError("invalid_question_digest")
    return value


def read_request(conn, timeout=1.5):
    deadline = time.monotonic() + timeout
    size = struct.unpack("!I", read_exact(conn, 4, deadline))[0]
    if not 2 <= size <= MAX_JSON:
        raise ValueError("invalid_metadata_size")
    value = validate_request(parse_json(read_exact(conn, size, deadline)))
    return value, read_exact(conn, value["image_size"], deadline)


def response_base(request):
    return {"protocol": PROTOCOL, "profile": PROFILE,
            "manifest_sha256": request["manifest_sha256"],
            **{k: request[k] for k in IDENTITIES}}


def failed_item(item, reason):
    return {**item, "status": "unknown", "reason": reason, "business_qualified": False}


def failure_response(request, reason):
    return {**response_base(request), "status": "unknown", "reason": reason,
            "items": [failed_item(item, reason) for item in request["items"]]}


def write_response(conn, value):
    payload = json.dumps(value, ensure_ascii=False, separators=(",", ":"), allow_nan=False).encode()
    if len(payload) > MAX_JSON:
        raise ValueError("response_too_large")
    conn.sendall(struct.pack("!I", len(payload)) + payload)
