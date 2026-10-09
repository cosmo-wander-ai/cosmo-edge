"""Local checks for a Sophon import package; never loads or decrypts a model."""

from __future__ import annotations

import hashlib
import re
import struct
from pathlib import Path
from typing import Any

import agent_workflow as core

# Keep in sync with src/nn/utils/model_header_info.h (little-endian CENN v1).
_HEADER = struct.Struct("<4sHHII8Q")
_MAX_U64 = (1 << 64) - 1


def inspect_container(path: Path) -> dict[str, Any]:
    """Validate CENN framing and hash payloads in bounded chunks, excluding the header."""
    with path.open("rb") as stream:
        header = stream.read(_HEADER.size)
        if header[:4] == b"CEMC":
            return {"status": "UNVERIFIED", "detail": "CEMC validation requires its authorized runtime."}
        if header[:4] == b"\x01\x00\x01\xec":
            return {"status": "FAIL", "detail": "Legacy encrypted model format is rejected by the current production load policy."}
        if header[:4] != b"CENN":
            return {"status": "FAIL", "detail": "model.nn is not a supported CENN container (renaming a bmodel is insufficient)."}
        if len(header) != _HEADER.size:
            return {"status": "FAIL", "detail": "CENN header is truncated."}
        _, version, header_size, count, reserved, *sizes = _HEADER.unpack(header)
        if version != 1:
            return {"status": "UNVERIFIED", "detail": f"CENN version {version} is not supported by this checker."}
        error = None
        if header_size != _HEADER.size:
            error = "header size must be 80"
        elif not 1 <= count <= 8:
            error = "model count must be in range 1..8"
        elif reserved:
            error = "reserved field must be zero"
        elif any(size == 0 for size in sizes[:count]):
            error = "model segment sizes must be positive"
        elif any(sizes[count:]):
            error = "unused model size slots must be zero"
        elif _HEADER.size + sum(sizes) > _MAX_U64:
            error = "model file size overflow"
        elif _HEADER.size + sum(sizes) != path.stat().st_size:
            error = "declared segment sizes differ from the actual file size"
        if error:
            return {"status": "FAIL", "detail": "Invalid CENN: " + error + "."}
        segments = []
        for index, size in enumerate(sizes[:count]):
            digest = hashlib.sha256()
            remaining = size
            while remaining:
                chunk = stream.read(min(remaining, 1024 * 1024))
                if not chunk:
                    return {"status": "FAIL", "detail": "CENN payload became truncated while reading."}
                digest.update(chunk)
                remaining -= len(chunk)
            segments.append({"index": index, "sizeBytes": size, "sha256": digest.hexdigest()})
        if stream.read(1):
            return {"status": "FAIL", "detail": "CENN has trailing bytes."}
    return {"status": "PASS", "detail": "CENN v1 framing and file size are valid.", "segments": segments}


def _report_shapes(text: str, kind: str) -> list[list[int]] | None:
    # model_tool --info emits e.g. input: images, [1, 3, 320, 320], float32, scale: 1
    # Require direction-labelled lines; an unordered shape occurrence is not I/O evidence.
    lines = re.findall(rf"^\s*{kind}\s*:\s*(.+)$", text, flags=re.MULTILINE | re.IGNORECASE)
    shapes = []
    for line in lines:
        match = re.search(r"\[\s*(\d+(?:\s*,\s*\d+)*)\s*\]", line)
        if not match:
            return None
        shape = [int(value.strip()) for value in match.group(1).split(",")]
        if not all(shape):
            return None
        shapes.append(shape)
    return shapes or None


def check_package(
    config: dict[str, Any],
    model_path: Path,
    run_dir: Path,
    parameters: dict[str, Any],
    artifacts: list[dict[str, Any]],
    model_info: dict[str, Any],
) -> dict[str, Any]:
    """Check only evidence available in the current frozen conversion run."""
    checks: list[dict[str, Any]] = []

    def add(field: str, status: str, detail: str, **values: Any) -> None:
        checks.append({"field": field, "status": status, "detail": detail, **values})

    def compare(field: str, actual: Any, expected: Any) -> None:
        add(field, "PASS" if actual == expected else "FAIL", "Compare with the frozen candidate contract.",
            actual=actual, expected=expected)

    container = inspect_container(model_path)
    add("model.nn", container["status"], container["detail"])
    compare("chip_type", str(config.get("chip_type", "")).lower(), parameters["targetChip"])
    models = config.get("models")
    if not isinstance(models, list) or not models or any(not isinstance(item, dict) for item in models):
        add("models", "FAIL", "models must be a non-empty array of model objects.")
        models = []
    effective_batches = []
    for index, model in enumerate(models):
        # ParsePipelineConfig defaults absent/null max_batch to 1. The runtime then
        # overwrites every input shape's first axis (GetInputShapesMap).
        batch = model.get("max_batch", 1)
        batch = 1 if batch is None else batch
        if type(batch) is not int or batch > (1 << 31) - 1:
            add(f"models[{index}].max_batch", "UNVERIFIED", "max_batch is not a supported integer configuration value.")
            batch = None
        elif batch < 1:
            add(f"models[{index}].max_batch", "FAIL", "Effective max_batch must be at least 1.", actual=batch)
        effective_batches.append(batch)
        for kind in ("inputs", "outputs"):
            tensors = model.get(kind)
            if not isinstance(tensors, list) or not tensors or any(
                not isinstance(tensor, dict) or not isinstance(tensor.get("shape"), list)
                or not tensor["shape"] or any(
                    type(dim) is not int or (axis > 0 and dim <= 0)
                    or (kind == "outputs" and axis == 0 and dim != -1 and dim <= 0)
                    for axis, dim in enumerate(tensor["shape"])
                )
                for tensor in tensors
            ):
                add(f"models[{index}].{kind}", "FAIL", "Non-batch axes must be positive integers; output batch may use -1.")
    segments = container.get("segments", [])
    bmodels = [item for item in artifacts if str(item.get("path", "")).lower().endswith(".bmodel")]
    if segments:
        compare("models.count", len(models), len(segments))
        for segment in segments:
            matches = [item for item in bmodels if item.get("sha256") == segment["sha256"]
                       and item.get("sizeBytes") == segment["sizeBytes"]]
            status = "PASS" if matches else ("FAIL" if len(bmodels) == len(segments) else "UNVERIFIED")
            add(f"segments[{segment['index']}].artifact", status,
                "Payload matches a current hash-verified bmodel." if matches else
                "Payload does not match a current hash-verified bmodel; candidate identity is unverified or differs.",
                sha256=segment["sha256"], artifacts=[item["path"] for item in matches])

    # The existing executor records one bmodel and one modelInfo report. Do not invent
    # a config-to-report mapping for multi-model packages or multi-stage reports.
    single = len(models) == len(segments) == len(bmodels) == 1
    if not single:
        add("modelInfo.mapping", "UNVERIFIED", "A single candidate/config/report mapping is unavailable; per-segment I/O evidence is required.")
    else:
        model = models[0]
        for kind, key in (("inputs", "inputShapes"), ("outputs", "expectedOutputShapes")):
            actual = [item.get("shape") if isinstance(item, dict) else None
                      for item in model.get(kind, [])] if isinstance(model.get(kind), list) else None
            expected = parameters.get(key)
            if actual and expected:
                effective = []
                for index, shape in enumerate(actual):
                    if not isinstance(shape, list) or not shape:
                        effective.append(shape)
                    elif kind == "inputs":
                        effective.append([effective_batches[0], *shape[1:]])
                    elif shape[0] == -1 and index < len(expected):
                        # SophonNetNode takes output shapes from BMRT metadata;
                        # -1 is also used by the shipped feature template.
                        effective.append([expected[index][0], *shape[1:]])
                    else:
                        effective.append(shape)
                if kind != "inputs" or effective_batches[0] is not None:
                    compare(f"models[0].{kind}.effectiveShape", effective, expected)
            elif expected:
                compare(f"models[0].{kind}.shape", actual, expected)
            else:
                add(f"models[0].{kind}.shape", "UNVERIFIED", "The frozen contract has no expected shapes.")
        shapes = parameters["inputShapes"]
        layout = parameters["inputLayout"]
        params = model.get("params", {})
        model_type = config.get("model_type")
        if not isinstance(params, dict):
            add("models[0].params", "UNVERIFIED", "Preprocessing parameters are not an object.")
        elif len(shapes) != 1 or len(shapes[0]) != 4 or layout not in ("NCHW", "NHWC"):
            add("models[0].params.spatialSize", "UNVERIFIED", "No unambiguous image input layout is frozen.")
        elif model_type not in ("yolov8_det", "classify", "feature"):
            add("models[0].params.spatialSize", "UNVERIFIED", "This pipeline's preprocessing size contract is not implemented by the checker.")
        else:
            # Known pipeline defaults, not universal candidate dimensions. See
            # detection_pipeline.cc, classify_pipeline.cc and feature_pipeline.cc.
            field = "input_size"
            defaults = {"yolov8_det": [640, 640], "classify": [224, 224], "feature": [112, 112]}[model_type]
            selector_known = True
            if model_type == "feature":
                crop = params.get("crop", False)
                crop = False if crop is None else crop
                affine = params.get("use_affine_crop", not crop if type(crop) is bool else None)
                if type(affine) is not bool:
                    add("models[0].params.use_affine_crop", "UNVERIFIED", "Cannot determine the feature preprocessing branch.")
                    selector_known = False
                elif affine:
                    field = "output_hw"
            if selector_known:
                actual = params.get(field, defaults)
                actual = defaults if actual is None else actual
                if not isinstance(actual, list) or len(actual) < 2 or any(type(dim) is not int for dim in actual):
                    add(f"models[0].params.{field}", "UNVERIFIED", "Cannot determine the effective preprocessing size from this value.")
                else:
                    # Sophon resize/affine nodes consume height then width.
                    spatial = shapes[0][2:4] if layout == "NCHW" else shapes[0][1:3]
                    compare(f"models[0].params.{field}", actual[:2], spatial)

        observed: dict[str, list[list[int]]] = {}
        if model_info.get("status") != "PASS" or model_info.get("contractMatches") is not True:
            add("modelInfo", "UNVERIFIED", "Passing model_tool evidence is unavailable.")
        else:
            try:
                report = core.resolve_run_input(run_dir, str(model_info.get("report", "")))
                report_sha256 = model_info.get("reportSha256", "")
                if not isinstance(report_sha256, str) or not re.fullmatch(r"[0-9a-f]{64}", report_sha256):
                    add("modelInfo.reportSha256", "UNVERIFIED", "The model_tool report identity is not frozen.")
                elif core.sha256_file(report) != report_sha256:
                    add("modelInfo.reportSha256", "FAIL", "The recorded model_tool report changed.")
                else:
                    text = report.read_text(encoding="utf-8")
                    for kind, key in (("input", "inputShapes"), ("output", "expectedOutputShapes")):
                        reported = _report_shapes(text, kind)
                        if reported is None:
                            add(f"modelInfo.{kind}", "UNVERIFIED", "Direction-labelled model_tool shapes could not be read.")
                        elif not parameters.get(key):
                            add(f"modelInfo.{kind}", "UNVERIFIED", "No frozen expected shapes are available.")
                        elif len(reported) != len(parameters[key]):
                            add(f"modelInfo.{kind}", "UNVERIFIED", "Report tensor count cannot be mapped to this candidate (possibly multiple networks/stages).")
                        else:
                            compare(f"modelInfo.{kind}", reported, parameters[key])
                            if reported == parameters[key]:
                                observed[kind] = reported
            except (core.WorkflowError, OSError, UnicodeError):
                add("modelInfo", "UNVERIFIED", "The recorded model_tool report is missing or unreadable.")

        # Only this explicit product decoder contract implies features = 4 + classes.
        if config.get("model_type") == "yolov8_det":
            outputs = observed.get("output", [])
            if len(outputs) == 1 and len(outputs[0]) == 3 and min(outputs[0][1:]) > 4 and outputs[0][1] != outputs[0][2]:
                classes = min(outputs[0][1:]) - 4
                labels = config.get("labels")
                if not isinstance(labels, list):
                    add("labels", "FAIL", "YOLOv8 labels must be an array; intentional subsets are allowed.")
                else:
                    ids = []
                    for item in labels:
                        raw = item.get("id") if isinstance(item, dict) else None
                        if type(raw) is int or (isinstance(raw, str) and re.fullmatch(r"[0-9]+", raw)):
                            ids.append(int(raw))
                        else:
                            add("labels.id", "FAIL", "Label IDs must be non-negative integers.")
                    if len(set(ids)) != len(ids) or any(value < 0 or value >= classes for value in ids):
                        add("labels.id", "FAIL", "Duplicate or out-of-range YOLOv8 label IDs.", actual=ids, classCount=classes)
                    elif len(ids) == len(labels):
                        add("labels.id", "PASS", "Label IDs are unique and within the observed YOLOv8 decoder range; subsets are allowed.", classCount=classes)
            else:
                add("labels.id", "UNVERIFIED", "YOLOv8 class range cannot be inferred from verified output shapes.")

    statuses = {item["status"] for item in checks}
    status = "FAIL" if "FAIL" in statuses else "UNVERIFIED" if "UNVERIFIED" in statuses else "PASS"
    return {
        "id": "S4", "status": status,
        "detail": "Local package framing, payload identity and available shape/configuration checks; no runtime or business acceptance.",
        "checks": checks,
        "unverifiedFields": ["tensor names and dtypes", "preprocessing values", "label semantics", "device runtime"],
    }
