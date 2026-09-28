#!/usr/bin/env python3
"""Extract ordered one-to-one YOLO26 Detect box/class heads for host decoding."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path

import onnx


HEAD_PATTERN = re.compile(
    r"^/.+/one2one_cv([23])\.([0-2])/one2one_cv\1\.\2\.2/Conv_output_0$"
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def extract(source: Path, output: Path, report: Path) -> dict:
    inferred = onnx.shape_inference.infer_shapes(onnx.load(source))
    if len(inferred.graph.input) != 1:
        raise RuntimeError("YOLO26 Detect extraction requires one image input")
    values = {
        item.name: item for item in list(inferred.graph.value_info) + list(inferred.graph.output)
    }
    names: dict[tuple[int, int], str] = {}
    for node in inferred.graph.node:
        if node.op_type != "Conv":
            continue
        for name in node.output:
            match = HEAD_PATTERN.fullmatch(name)
            if match:
                key = (int(match.group(2)), int(match.group(1)))
                if key in names:
                    raise RuntimeError(f"ambiguous YOLO26 Detect head: {key}")
                names[key] = name
    ordered_keys = [(branch, kind) for branch in range(3) for kind in (2, 3)]
    if set(names) != set(ordered_keys):
        raise RuntimeError("source does not contain three one-to-one YOLO26 box/class head pairs")
    outputs = []
    class_count = None
    previous_height = None
    for branch, kind in ordered_keys:
        name = names[(branch, kind)]
        dims = [dim.dim_value for dim in values[name].type.tensor_type.shape.dim]
        if len(dims) != 4 or dims[0] != 1 or min(dims[1:]) <= 0:
            raise RuntimeError(f"invalid YOLO26 head shape: {name}: {dims}")
        if kind == 2 and dims[1] != 4:
            raise RuntimeError(f"YOLO26 is expected to be DFL-free with four box channels: {name}")
        if kind == 3:
            if dims[2:] != outputs[-1]["shape"][2:]:
                raise RuntimeError("YOLO26 box/class spatial shapes differ")
            if class_count is not None and class_count != dims[1]:
                raise RuntimeError("YOLO26 class counts differ across scales")
            class_count = dims[1]
        if kind == 2:
            if previous_height is not None and dims[2] >= previous_height:
                raise RuntimeError("YOLO26 heads are not ordered from fine to coarse scale")
            previous_height = dims[2]
        outputs.append({"name": name, "shape": dims})

    selected = [item["name"] for item in outputs]
    extracted = onnx.utils.Extractor(inferred).extract_model([inferred.graph.input[0].name], selected)
    onnx.checker.check_model(extracted)
    output.parent.mkdir(parents=True, exist_ok=True)
    onnx.save(extracted, output)
    report_data = {
        "schema_version": 1,
        "source": {"path": str(source), "sha256": sha256(source)},
        "extracted": {"path": str(output), "sha256": sha256(output), "outputs": outputs},
        "host_output_adapter": "yolo26_one2one_6head_v1",
    }
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(report_data, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return report_data


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()
    source = args.input.resolve()
    output = args.output.resolve()
    report = (args.report or output.with_suffix(output.suffix + ".provenance.json")).resolve()
    if not source.is_file():
        parser.error(f"input does not exist: {source}")
    for candidate in (output, report):
        if candidate.exists() and not args.force:
            parser.error(f"refusing to overwrite {candidate}; pass --force")
    print(json.dumps(extract(source, output, report), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
