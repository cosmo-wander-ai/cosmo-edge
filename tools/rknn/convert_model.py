#!/usr/bin/env python3
"""Build a reproducible RKNN artifact from an ONNX model specification."""

from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

import onnx
from rknn.api import RKNN

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from conversion_common import validate_input_sizes


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_success(code: int, action: str) -> None:
    if code != 0:
        raise RuntimeError(f"{action} failed with RKNN code {code}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--spec", required=True, type=Path)
    parser.add_argument(
        "--platform-profile",
        type=Path,
        help="RKNN platform profile; supplies the chip-specific target_platform",
    )
    parser.add_argument("--model", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--dataset", type=Path, help="RKNN calibration dataset list")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--quantize", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--input-sizes", help="Three [height, width] pairs as JSON")
    args = parser.parse_args()

    spec_path = args.spec.resolve()
    platform_profile_path = args.platform_profile.resolve() if args.platform_profile else None
    model_path = args.model.resolve()
    output_path = args.output.resolve()
    report_path = (args.report or output_path.with_suffix(output_path.suffix + ".build.json")).resolve()
    if not spec_path.is_file() or not model_path.is_file():
        parser.error("--spec and --model must be existing files")
    if platform_profile_path is not None and not platform_profile_path.is_file():
        parser.error("--platform-profile must be an existing file")
    if args.quantize and (args.dataset is None or not args.dataset.is_file()):
        parser.error("--quantize requires an existing --dataset list")
    if not args.quantize and args.dataset is not None:
        parser.error("--dataset is only valid with --quantize")
    for candidate in (output_path, report_path):
        if candidate.exists() and not args.force:
            parser.error(f"refusing to overwrite {candidate}; pass --force")

    spec = json.loads(spec_path.read_text(encoding="utf-8"))
    conversion = spec["conversion"]
    declared_sizes = conversion.get("input_sizes")
    if args.input_sizes:
        requested_sizes = json.loads(args.input_sizes)
        if declared_sizes is not None and declared_sizes != requested_sizes:
            raise RuntimeError("input size profiles conflict with the model spec")
        declared_sizes = requested_sizes
    limit = conversion.get("input_shapes", [])
    if declared_sizes and not limit:
        if (isinstance(declared_sizes, list) and len(declared_sizes) == 3
                and all(isinstance(size, list) and len(size) == 2 for size in declared_sizes)):
            limit = [[1, 3, max(size[0] for size in declared_sizes), max(size[1] for size in declared_sizes)]]
    input_sizes = validate_input_sizes(declared_sizes, limit)
    profile = None
    if platform_profile_path is not None:
        profile = json.loads(platform_profile_path.read_text(encoding="utf-8"))
        if profile.get("backend") != "rknn":
            raise RuntimeError("platform profile backend must be rknn")
        target_platform = profile.get("conversion", {}).get("target_platform")
        if not isinstance(target_platform, str) or not target_platform:
            raise RuntimeError("platform profile must define conversion.target_platform")
        legacy_target = conversion.get("target_platform")
        if legacy_target and legacy_target != target_platform:
            raise RuntimeError(
                f"model spec target_platform={legacy_target} conflicts with platform profile "
                f"target_platform={target_platform}"
            )
    else:
        target_platform = conversion.get("target_platform")
        if not isinstance(target_platform, str) or not target_platform:
            parser.error("--platform-profile is required when the model spec is target-independent")
    expected_hash = spec["conversion"].get("input_sha256", spec.get("source_sha256"))
    actual_hash = sha256(model_path)
    if expected_hash and actual_hash != expected_hash:
        raise RuntimeError(
            f"source SHA-256 mismatch: expected {expected_hash}, got {actual_hash}; "
            "use a spec that identifies this exact model"
        )

    model = onnx.load(model_path)
    onnx.checker.check_model(model)
    runtime_outputs = spec.get("runtime_outputs")
    if runtime_outputs:
        actual_outputs = [item.name for item in model.graph.output]
        expected_outputs = [item["name"] for item in runtime_outputs]
        if actual_outputs != expected_outputs:
            raise RuntimeError(
                f"runtime output contract mismatch: expected {expected_outputs}, got {actual_outputs}"
            )
    opsets = {item.domain: item.version for item in model.opset_import}
    default_opset = opsets.get("", 0)
    maximum_opset = int(spec["conversion"]["maximum_onnx_opset"])
    if default_opset > maximum_opset:
        raise RuntimeError(f"ONNX opset {default_opset} exceeds locked RKNN maximum {maximum_opset}")
    maximum_ir = spec["conversion"].get("maximum_onnx_ir_version")
    if maximum_ir is not None and model.ir_version > int(maximum_ir):
        raise RuntimeError(f"ONNX IR {model.ir_version} exceeds locked maximum {maximum_ir}")
    if spec["conversion"].get("preprocessing_owner") != "host":
        raise RuntimeError("P0-P4 contract requires preprocessing_owner=host")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    rknn = RKNN(verbose=args.verbose)
    try:
        dynamic_config = ({"dynamic_input": [[[1, 3, height, width]] for height, width in input_sizes]}
                          if input_sizes else {})
        # Mean/std are intentionally omitted: CosmoEdge already supplies normalized
        # NCHW float tensors and must remain the single preprocessing owner.
        require_success(
            rknn.config(
                target_platform=target_platform,
                optimization_level=int(conversion["optimization_level"]),
                **dynamic_config,
            ),
            "rknn.config",
        )
        require_success(rknn.load_onnx(model=str(model_path)), "rknn.load_onnx")
        dataset = str(args.dataset.resolve()) if args.dataset else None
        require_success(
            rknn.build(do_quantization=args.quantize, dataset=dataset),
            "rknn.build",
        )
        require_success(rknn.export_rknn(str(output_path)), "rknn.export_rknn")
    finally:
        rknn.release()

    report = {
        "schema_version": 1,
        "created_at": datetime.now(timezone.utc).isoformat(),
        "rknn_toolkit2_version": importlib.metadata.version("rknn-toolkit2"),
        "spec": {"path": str(spec_path), "sha256": sha256(spec_path)},
        "platform_profile": (
            {
                "path": str(platform_profile_path),
                "sha256": sha256(platform_profile_path),
                "chip": profile.get("chip"),
            }
            if platform_profile_path is not None and profile is not None
            else None
        ),
        "source": {
            "path": str(model_path),
            "sha256": actual_hash,
            "ir_version": model.ir_version,
            "opsets": opsets,
            "outputs": [item.name for item in model.graph.output],
        },
        "build": {
            "target_platform": target_platform,
            "optimization_level": conversion["optimization_level"],
            "quantized": args.quantize,
            "input_sizes": input_sizes,
            "dataset": str(args.dataset.resolve()) if args.dataset else None,
            "dataset_sha256": sha256(args.dataset.resolve()) if args.dataset else None,
            "preprocessing_owner": "host",
        },
        "artifact": {
            "path": str(output_path),
            "sha256": sha256(output_path),
            "bytes": output_path.stat().st_size,
        },
    }
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
