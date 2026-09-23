"""Versioned answer parsing; only the level protocol accepts complete JSON fences."""
import json
import re

LEVELS = ("I", "II", "III", "IV")
JOINT_PROTOCOL = "inspecsafe_rgb_en_joint_v1"
LEVEL_PROTOCOL = "inspecsafe_rgb_level_v2"
PROTOCOLS = (JOINT_PROTOCOL, LEVEL_PROTOCOL)
_JSON_FENCE = re.compile(r"```(?:json)?[ \t]*\r?\n(.*?)\r?\n[ \t]*```",
                         re.DOTALL | re.IGNORECASE | re.ASCII)


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate_field")
        result[key] = value
    return result


def parse_output(raw, protocol=JOINT_PROTOCOL):
    if protocol not in PROTOCOLS:
        raise ValueError("unsupported output protocol")
    result = dict(description=None, safety_level=None, description_valid=False,
                  safety_level_valid=False, joint_valid=False, protocol_valid=False,
                  strict_valid=False, markdown_fence=False, markdown_recovered=False, error=None)
    if not isinstance(raw, str) or not raw.strip():
        result["error"] = "empty_output"
        return result
    payload = raw
    if protocol == LEVEL_PROTOCOL:
        fence = _JSON_FENCE.fullmatch(raw.strip(" \t\r\n"))
        if fence:
            payload = fence.group(1)
            result["markdown_fence"] = True
    try:
        value = json.loads(payload, object_pairs_hook=_unique_object,
                           parse_constant=lambda value: (_ for _ in ()).throw(ValueError("non_json_constant")))
        if not isinstance(value, dict):
            raise ValueError("non_object")
        allowed = {"safety_level"} if protocol == LEVEL_PROTOCOL else {"description", "safety_level"}
        if set(value) - allowed:
            raise ValueError("extra_field")
    except (ValueError, RecursionError) as exc:
        result["error"] = "parse_failure"
        result["detail"] = str(exc)
        return result
    description = value.get("description")
    level = value.get("safety_level")
    if isinstance(level, str):
        level = level.strip()
    result["description_valid"] = isinstance(description, str) and bool(description.strip())
    result["safety_level_valid"] = isinstance(level, str) and level in LEVELS
    result["description"] = description.strip() if result["description_valid"] else None
    result["safety_level"] = level if result["safety_level_valid"] else None
    result["protocol_valid"] = (result["safety_level_valid"] if protocol == LEVEL_PROTOCOL else
                                result["description_valid"] and result["safety_level_valid"])
    # Keep the existing completion field usable by consumers of either protocol.
    result["joint_valid"] = result["protocol_valid"]
    result["strict_valid"] = result["protocol_valid"] and not result["markdown_fence"]
    result["markdown_recovered"] = result["protocol_valid"] and result["markdown_fence"]
    if not result["protocol_valid"]:
        result["error"] = "invalid_fields"
    return result
