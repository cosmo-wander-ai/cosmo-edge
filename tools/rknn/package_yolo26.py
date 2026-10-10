#!/usr/bin/env python3
"""Package a converted YOLO26 RKNN Detect model for CosmoEdge import."""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import re
import shutil
from pathlib import Path

import onnx


def digest(path: Path, algorithm: str) -> str:
    value = hashlib.new(algorithm)
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-onnx", required=True, type=Path)
    parser.add_argument("--model", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--algorithm-code", required=True)
    parser.add_argument("--chip", required=True)
    parser.add_argument("--report", required=True, type=Path)
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9]{7}", args.algorithm_code):
        parser.error("--algorithm-code must be a seven-digit model identifier")
    if not re.fullmatch(r"rk[0-9]+|rv[0-9]+[a-z]?", args.chip.lower()):
        parser.error("--chip must be an RK/RV chip identifier")
    if not args.source_onnx.is_file() or not args.model.is_file():
        parser.error("--source-onnx and --model must exist")
    if args.output_dir.exists() or args.report.exists():
        parser.error("refusing to overwrite an existing package or report")

    model = onnx.load(args.source_onnx, load_external_data=False)
    metadata = {item.key: item.value for item in model.metadata_props}
    names = ast.literal_eval(metadata.get("names", ""))
    if not isinstance(names, dict) or not names or set(names) != set(range(len(names))):
        raise ValueError("source ONNX must contain contiguous class names in its metadata")
    template = Path(__file__).resolve().parents[2] / "data/resource/aiboxresource_x86/model_template/yolo26_det.json"
    config = json.loads(template.read_text(encoding="utf-8"))
    config["algorithm_code"] = args.algorithm_code
    config["chip_type"] = args.chip.upper()
    config["models"][0]["file_name"] = "model.rknn"
    config["models"][0]["file_md5"] = digest(args.model, "md5")
    config["labels"] = [
        {"id": str(index), "name": str(names[index]), "threshold": [0.25, 0.25]}
        for index in range(len(names))
    ]
    args.output_dir.mkdir(parents=True, mode=0o700)
    shutil.copyfile(args.model, args.output_dir / "model.rknn")
    config_path = args.output_dir / "config.json"
    config_path.write_text(json.dumps(config, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    config_path.chmod(0o600)
    report = {
        "schema_version": 1,
        "package_directory": args.output_dir.name,
        "algorithm_code": args.algorithm_code,
        "chip": args.chip.lower(),
        "class_count": len(names),
        "source_onnx_sha256": digest(args.source_onnx, "sha256"),
        "model_sha256": digest(args.output_dir / "model.rknn", "sha256"),
        "config_sha256": digest(config_path, "sha256"),
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
