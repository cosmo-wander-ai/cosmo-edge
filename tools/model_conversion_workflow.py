#!/usr/bin/env python3
"""Measured ONNX-to-Sophon conversion and evidence workflow."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Any

import agent_workflow as core
from conversion_common import (
    ExecutionFailure,
    utc_now,
    _require_string,
    conversion_parameters as _common_conversion_parameters,
    _read_environment_report,
    _read_asset_selection,
    _run_relative,
    _write_private_text,
    _run_logged,
    _artifact,
    _failed_manifest,
    _attempt_summary,
    _archive_previous_attempt,
    _data_flow_record,
)


PROJECT_ROOT = Path(__file__).resolve().parents[1]
EXAMPLE_STATUS_CANDIDATE = "candidate"
EXAMPLE_STATUS_CONVERSION_VERIFIED = "conversion-verified"
LEGACY_EXAMPLE_STATUS_VERIFIED = "verified"
EXAMPLE_LIFECYCLE_ACTIVE = "active"
EXAMPLE_LIFECYCLE_REVOKED = "revoked"
CONVERSION_COVERAGE = [
    "onnx-preflight",
    "transform",
    "deploy",
    "model-info",
    "tensor-compare",
]
CONVERSION_KNOWN_LIMITS = [
    "device import and runtime are not covered",
    "business accuracy acceptance is not covered",
]
DEVICE_VALIDATION_NONE = {
    "status": "none",
    "knownLimits": ["device import and runtime are not covered by this example"],
}
SEAL_VERSION = "1.0"


def conversion_parameters(contract: dict[str, Any], run_dir: Path) -> dict[str, Any]:
    parameters = _common_conversion_parameters(contract, run_dir)
    for key in ("quantizeTable", "calibrationTable"):
        raw = parameters["raw"].get(key)
        parameters[key] = None
        if raw is None:
            continue
        path = core.resolve_run_input(run_dir, _require_string(raw, f"parameters.{key}"))
        try:
            rows = [
                line.split()
                for line in path.read_text(encoding="utf-8").splitlines()
                if line.strip() and not line.lstrip().startswith("#")
            ]
        except (OSError, UnicodeError) as error:
            raise core.WorkflowError(f"parameters.{key} must be a readable UTF-8 table") from error
        if not rows:
            raise core.WorkflowError(f"parameters.{key} must contain table entries")
        if key == "quantizeTable":
            if any(len(row) != 2 or row[1] not in {"F32", "F16", "BF16", "INT8"} for row in rows):
                raise core.WorkflowError("quantizeTable entries must be layer-name F32/F16/BF16/INT8")
        else:
            for row in rows:
                try:
                    threshold, minimum, maximum = (float(value) for value in row[1:])
                    valid = len(row) == 4 and all(
                        math.isfinite(value) for value in (threshold, minimum, maximum)
                    ) and threshold > 0 and minimum <= maximum
                except ValueError:
                    valid = False
                if not valid:
                    raise core.WorkflowError(
                        "calibrationTable entries must be tensor-name positive-threshold finite-min finite-max"
                    )
        parameters[key] = path
    if parameters["quantization"] == "INT8" and parameters["calibrationTable"] is None:
        raise core.WorkflowError("INT8 conversion requires parameters.calibrationTable")
    return parameters


def _conversion_inputs(parameters: dict[str, Any], run_dir: Path) -> list[dict[str, Any]]:
    return [
        _artifact(core.resolve_run_input(run_dir, str(parameters[key])), run_dir, role)
        for key, role in (
            ("quantizeTable", "quantization-table"),
            ("calibrationTable", "calibration-table"),
        )
        if parameters.get(key) is not None
    ]


def _conversion_input_failures(
    manifest: dict[str, Any], parameters: dict[str, Any], run_dir: Path,
) -> list[str]:
    try:
        current = _conversion_inputs(parameters, run_dir)
    except (core.WorkflowError, OSError) as error:
        return [f"Conversion table input is unavailable: {error}"]
    if manifest.get("conversionInputs", []) != current:
        return ["Conversion table inputs changed or are not bound to the execution manifest; rerun conversion."]
    return []


def _container_path(path: Path, run_dir: Path) -> str:
    return f"/workspace/run/{_run_relative(path, run_dir)}"


def _docker_base(
    run_dir: Path, image_id: str, entrypoint: str, toolchain: dict[str, Any] | None = None,
) -> list[str]:
    return [
        "docker",
        "run",
        "--rm",
        "--network",
        "none",
        "--user",
        f"{os.getuid()}:{os.getgid()}",
        "--env",
        "HOME=/tmp",
        "--tmpfs",
        "/tmp:rw,nosuid,nodev,exec,size=2147483648",
        "--volume",
        f"{run_dir.resolve()}:/workspace/run:rw",
        "--workdir",
        "/workspace/run/work",
        *core.source_toolchain.docker_arguments(toolchain or {}),
        "--entrypoint",
        entrypoint,
        image_id,
    ]


def _runtime_path(path: Path, run_dir: Path, toolchain: dict[str, Any]) -> str:
    if toolchain.get("kind") == "container-image":
        return _container_path(path, run_dir)
    return str(path.resolve())


def _tool_command(
    toolchain: dict[str, Any],
    tool_key: str,
    arguments: list[str],
    *,
    run_dir: Path,
) -> list[str]:
    entry = toolchain.get("tools", {}).get(tool_key)
    if not isinstance(entry, dict) or not isinstance(entry.get("path"), str):
        raise core.WorkflowError(f"admitted toolchain has no {tool_key} command")
    python_executable = str(toolchain.get("pythonExecutable", ""))
    if not python_executable:
        raise core.WorkflowError("admitted toolchain has no Python executable")
    invocation = entry.get("invocation", "python")
    command = (
        [entry["path"], *arguments]
        if invocation == "direct"
        else [python_executable, entry["path"], *arguments]
    )
    if toolchain.get("kind") == "python-package":
        return command
    if toolchain.get("kind") == "container-image":
        image_id = toolchain.get("image", {}).get("id")
        if not isinstance(image_id, str) or not image_id:
            raise core.WorkflowError("admitted container toolchain has no image identity")
        entrypoint = entry["path"] if invocation == "direct" else python_executable
        container_command = _docker_base(run_dir, image_id, entrypoint, toolchain)
        if invocation != "direct":
            container_command.append(entry["path"])
        return [*container_command, *arguments]
    raise core.WorkflowError("admitted toolchain kind is unsupported")


def _tool_help(
    toolchain: dict[str, Any],
    tool_key: str,
    *,
    run_dir: Path,
    log_path: Path,
    commands: list[str],
) -> str:
    process = _run_logged(
        _tool_command(toolchain, tool_key, ["--help"], run_dir=run_dir),
        cwd=PROJECT_ROOT,
        log_path=log_path,
        commands=commands,
        run_dir=run_dir,
        timeout=60,
        environment=core.toolchain_environment(toolchain),
    )
    return f"{process.stdout}\n{process.stderr}" if process.returncode in (0, 1, 2) else ""


def build_transform_arguments(
    parameters: dict[str, Any],
    *,
    source_model: str,
    mlir: str,
    test_input: str | None = None,
    test_result: str | None = None,
) -> list[str]:
    arguments = [
        "--model_name",
        parameters["modelName"],
        "--model_def",
        source_model,
        "--input_shapes",
        json.dumps(parameters["inputShapes"], separators=(",", ":")),
        "--pixel_format",
        parameters["pixelFormat"],
        "--mlir",
        mlir,
    ]
    if test_input and test_result:
        arguments.extend(["--test_input", test_input, "--test_result", test_result])
    return arguments


def build_deploy_arguments(
    parameters: dict[str, Any],
    *,
    mlir: str,
    model: str,
    quantize_table: str | None = None,
    calibration_table: str | None = None,
    test_input: str | None = None,
    test_reference: str | None = None,
) -> list[str]:
    arguments = [
        "--mlir",
        mlir,
        "--quantize",
        parameters["quantization"],
        "--chip",
        parameters["toolchainChip"],
        "--model",
        model,
    ]
    for flag, value in (("--quantize_table", quantize_table), ("--calibration_table", calibration_table)):
        if value is not None:
            arguments.extend([flag, value])
    if test_input and test_reference:
        arguments.extend(["--test_input", test_input, "--test_reference", test_reference])
        if parameters.get("tensorTolerance"):
            arguments.extend(["--tolerance", parameters["tensorTolerance"]])
    return arguments


def _shape_appears(text: str, shape: list[int]) -> bool:
    compact = re.sub(r"\s+", "", text.lower())
    comma = "[" + ",".join(str(value) for value in shape) + "]"
    cross = "x".join(str(value) for value in shape)
    return comma in compact or cross in compact


def execute_conversion(
    contract_path: Path,
    run_dir: Path,
    contract: dict[str, Any],
    *,
    test_input_override: str | None = None,
) -> tuple[dict[str, Any], dict[str, Any]]:
    parameters = conversion_parameters(contract, run_dir)
    environment = _read_environment_report(contract_path, run_dir, contract)
    selection = _read_asset_selection(run_dir)
    admitted_toolchain = environment["toolchain"]
    current_toolchain, toolchain_error = core.inspect_toolchain(parameters["toolchainSpec"])
    if not current_toolchain:
        raise core.WorkflowError(
            f"admitted compiler toolchain is no longer available: {toolchain_error}"
        )
    if current_toolchain.get("id") != admitted_toolchain.get("id"):
        raise core.WorkflowError("toolchain identity changed after admission; rerun doctor")

    work_dir = run_dir / "work"
    artifacts_dir = run_dir / "artifacts"
    logs_dir = run_dir / "logs"
    verification_dir = run_dir / "verification"
    for directory in (work_dir, artifacts_dir, logs_dir, verification_dir):
        directory.mkdir(parents=True, exist_ok=True, mode=0o700)

    source_model = parameters["sourceModel"]
    model_name = parameters["modelName"]
    chip = parameters["targetChip"]
    quantization = parameters["quantization"].lower()
    mlir_path = work_dir / f"{model_name}.mlir"
    reference_path = work_dir / f"{model_name}_top_outputs.npz"
    bmodel_path = artifacts_dir / f"{model_name}_{chip}_{quantization}.bmodel"
    preflight_path = verification_dir / "onnx-check.json"
    model_info_path = verification_dir / "model-info.txt"
    manifest_path = run_dir / "execution-manifest.json"
    attempt, previous_attempts = _archive_previous_attempt(run_dir)
    commands: list[str] = []
    started = time.monotonic()
    manifest: dict[str, Any] = {
        "schemaVersion": "1.0",
        "status": "RUNNING",
        "attempt": attempt,
        "previousAttempts": previous_attempts,
        "runId": contract["runId"],
        "task": contract["task"],
        "commit": environment.get("commit"),
        "tree": environment.get("tree"),
        "repository": environment["repository"],
        "contractSha256": core.sha256_file(contract_path),
        "routeAssessment": "route-assessment.json",
        "routeAssessmentSha256": environment["routeAssessmentSha256"],
        "startedAt": utc_now(),
        "source": _artifact(source_model, run_dir, "source-onnx"),
        "conversionInputs": _conversion_inputs(parameters, run_dir),
        "target": {
            "backend": parameters["targetBackend"],
            "chip": chip,
            "toolchainChip": parameters["toolchainChip"],
            "quantization": parameters["quantization"],
            "inputLayout": parameters["inputLayout"],
            "inputShapes": parameters["inputShapes"],
            "expectedOutputShapes": parameters["expectedOutputShapes"],
        },
        "toolchain": current_toolchain,
        "dataFlow": _data_flow_record(contract),
        "selectedAssets": core.redact_data(selection["selectedAssets"]),
        "assetDifferences": core.redact_data(selection["differences"]),
        "runtimeOverrides": {},
        "commands": commands,
        "stages": {},
        "artifacts": [],
    }
    core.atomic_write_json(manifest_path, manifest)

    try:
        python_name = parameters["pythonExecutable"]
        python_path = shutil.which(python_name) if not Path(python_name).is_absolute() else python_name
        if not python_path:
            raise core.WorkflowError(f"Python executable is unavailable: {python_name}")
        preflight_command = [
            str(python_path),
            str(PROJECT_ROOT / "tools" / "check_onnx_model.py"),
            str(source_model),
            "--json",
            str(preflight_path),
        ]
        shape_overrides = parameters["raw"].get("onnxInputShapeOverrides", {})
        if not isinstance(shape_overrides, dict):
            raise core.WorkflowError("parameters.onnxInputShapeOverrides must be an object")
        for input_name, shape in shape_overrides.items():
            if not isinstance(input_name, str) or not isinstance(shape, list):
                raise core.WorkflowError("invalid ONNX input shape override")
            preflight_command.extend(["--shape", f"{input_name}={','.join(map(str, shape))}"])
        preflight = _run_logged(
            preflight_command,
            cwd=PROJECT_ROOT,
            log_path=logs_dir / "S1-onnx-preflight.log",
            commands=commands,
            run_dir=run_dir,
            timeout=300,
        )
        if preflight.returncode != 0:
            manifest["stages"]["onnxPreflight"] = {
                "status": "FAIL",
                "report": _run_relative(preflight_path, run_dir) if preflight_path.exists() else None,
            }
            raise ExecutionFailure("ONNX preflight failed")
        manifest["stages"]["onnxPreflight"] = {
            "status": "PASS",
            "report": _run_relative(preflight_path, run_dir),
            "reportSha256": core.sha256_file(preflight_path),
        }
        core.atomic_write_json(manifest_path, manifest)

        transform_help = _tool_help(
            current_toolchain,
            "modelTransform",
            run_dir=run_dir,
            log_path=logs_dir / "tool-model-transform-help.log",
            commands=commands,
        )
        deploy_help = _tool_help(
            current_toolchain,
            "modelDeploy",
            run_dir=run_dir,
            log_path=logs_dir / "tool-model-deploy-help.log",
            commands=commands,
        )
        tensor_core_flags_supported = all(
            flag in transform_help for flag in ("--test_input", "--test_result")
        ) and all(
            flag in deploy_help
            for flag in ("--test_input", "--test_reference")
        )
        tolerance_flag_supported = "--tolerance" in deploy_help
        tensor_flags_supported = bool(
            tensor_core_flags_supported
            and (not parameters["tensorTolerance"] or tolerance_flag_supported)
        )
        manifest["toolchain"]["tensorFlagsSupported"] = tensor_flags_supported
        manifest["toolchain"]["toleranceFlagSupported"] = tolerance_flag_supported

        test_input_raw = test_input_override or parameters["raw"].get("testInput")
        test_input = (
            core.resolve_run_input(run_dir, _require_string(test_input_raw, "test input"))
            if test_input_raw
            else None
        )
        if test_input_override and test_input:
            manifest["runtimeOverrides"]["testInput"] = _run_relative(test_input, run_dir)
        tensor_requested = test_input is not None
        tensor_enabled = bool(tensor_requested and tensor_flags_supported)
        runtime_test_input = (
            _runtime_path(test_input, run_dir, current_toolchain)
            if tensor_enabled and test_input
            else None
        )
        runtime_reference = (
            _runtime_path(reference_path, run_dir, current_toolchain)
            if tensor_enabled
            else None
        )

        transform_args = build_transform_arguments(
            parameters,
            source_model=_runtime_path(source_model, run_dir, current_toolchain),
            mlir=_runtime_path(mlir_path, run_dir, current_toolchain),
            test_input=runtime_test_input,
            test_result=runtime_reference,
        )
        transform = _run_logged(
            _tool_command(
                current_toolchain,
                "modelTransform",
                transform_args,
                run_dir=run_dir,
            ),
            cwd=work_dir,
            log_path=logs_dir / "S2-transform.log",
            commands=commands,
            run_dir=run_dir,
            environment=core.toolchain_environment(current_toolchain),
        )
        if transform.returncode != 0 or not mlir_path.is_file():
            manifest["stages"]["transform"] = {"status": "FAIL"}
            raise ExecutionFailure("model_transform failed or did not create the requested MLIR")
        manifest["stages"]["transform"] = {
            "status": "PASS",
            "artifact": _artifact(mlir_path, run_dir, "mlir-intermediate"),
        }
        core.atomic_write_json(manifest_path, manifest)

        input_failures = _conversion_input_failures(manifest, parameters, run_dir)
        if input_failures:
            raise ExecutionFailure("; ".join(input_failures))
        deploy_args = build_deploy_arguments(
            parameters,
            mlir=_runtime_path(mlir_path, run_dir, current_toolchain),
            model=_runtime_path(bmodel_path, run_dir, current_toolchain),
            quantize_table=(
                _runtime_path(parameters["quantizeTable"], run_dir, current_toolchain)
                if parameters["quantizeTable"] else None
            ),
            calibration_table=(
                _runtime_path(parameters["calibrationTable"], run_dir, current_toolchain)
                if parameters["calibrationTable"] else None
            ),
            test_input=runtime_test_input,
            test_reference=runtime_reference,
        )
        deploy = _run_logged(
            _tool_command(
                current_toolchain,
                "modelDeploy",
                deploy_args,
                run_dir=run_dir,
            ),
            cwd=work_dir,
            log_path=logs_dir / "S2-deploy.log",
            commands=commands,
            run_dir=run_dir,
            environment=core.toolchain_environment(current_toolchain),
        )
        if deploy.returncode != 0 or not bmodel_path.is_file():
            manifest["stages"]["deploy"] = {"status": "FAIL"}
            raise ExecutionFailure("model_deploy failed or did not create the requested bmodel")
        bmodel = _artifact(bmodel_path, run_dir, "bm1688-bmodel" if chip == "bm1688" else "bmodel")
        manifest["stages"]["deploy"] = {"status": "PASS", "artifact": bmodel}
        manifest["artifacts"] = [bmodel]

        if tensor_enabled:
            tensor_status = "PASS"
            tensor_detail = (
                "TPU-MLIR test input/reference comparison completed with the contract tolerance."
                if parameters["tensorTolerance"]
                else "TPU-MLIR test input/reference comparison completed with the admitted tool's default tolerance policy."
            )
        elif tensor_requested and not tensor_flags_supported:
            tensor_status = "UNVERIFIED"
            tensor_detail = "The admitted toolchain does not expose all required tensor comparison flags."
        else:
            tensor_status = "UNVERIFIED"
            tensor_detail = "No test input was supplied for transform/deploy tensor comparison."
        manifest["stages"]["tensorCompare"] = {
            "status": tensor_status,
            "detail": tensor_detail,
            "tolerance": parameters["tensorTolerance"],
            "tolerancePolicy": (
                "contract-override" if parameters["tensorTolerance"] else "tool-default"
            ),
            "reference": (
                _run_relative(reference_path, run_dir) if reference_path.is_file() else None
            ),
        }

        if current_toolchain.get("tools", {}).get("modelTool"):
            model_info = _run_logged(
                _tool_command(
                    current_toolchain,
                    "modelTool",
                    [
                        "--info",
                        _runtime_path(bmodel_path, run_dir, current_toolchain),
                    ],
                    run_dir=run_dir,
                ),
                cwd=work_dir,
                log_path=logs_dir / "S2-model-info.log",
                commands=commands,
                run_dir=run_dir,
                timeout=120,
                environment=core.toolchain_environment(current_toolchain),
            )
            info_text = f"{model_info.stdout}\n{model_info.stderr}"
            if model_info.returncode == 0:
                _write_private_text(model_info_path, core.redact_text(info_text))
                expected_shapes = parameters["inputShapes"] + parameters["expectedOutputShapes"]
                contract_matches = all(_shape_appears(info_text, shape) for shape in expected_shapes)
                manifest["stages"]["modelInfo"] = {
                    "status": "PASS" if contract_matches else "UNVERIFIED",
                    "contractMatches": contract_matches,
                    "report": _run_relative(model_info_path, run_dir),
                    "reportSha256": core.sha256_file(model_info_path),
                }
            else:
                manifest["stages"]["modelInfo"] = {
                    "status": "UNVERIFIED",
                    "contractMatches": False,
                    "detail": "model_tool --info failed; see the private run log.",
                }
        else:
            manifest["stages"]["modelInfo"] = {
                "status": "UNVERIFIED",
                "contractMatches": False,
                "detail": "model_tool is unavailable in the admitted compiler toolchain.",
            }

        if current_toolchain.get("layout") == "source-tree":
            completed_toolchain, error = core.inspect_toolchain(parameters["toolchainSpec"])
            if not completed_toolchain or completed_toolchain.get("id") != admitted_toolchain.get("id"):
                raise core.WorkflowError("source compiler changed during conversion; rerun doctor: " + error)
        input_failures = _conversion_input_failures(manifest, parameters, run_dir)
        if input_failures:
            raise ExecutionFailure("; ".join(input_failures))
        manifest["status"] = "COMPLETE"
        manifest["completedAt"] = utc_now()
        manifest["durationSeconds"] = round(time.monotonic() - started, 3)
        core.atomic_write_json(manifest_path, manifest)
        return manifest, parameters
    except (core.WorkflowError, ExecutionFailure) as error:
        _failed_manifest(manifest_path, manifest, str(error))
        raise


def _normalize_example_record(record: Any) -> dict[str, Any]:
    if not isinstance(record, dict):
        raise core.WorkflowError("example record must be an object")
    normalized = json.loads(json.dumps(record))
    status = normalized.get("status")
    if status == LEGACY_EXAMPLE_STATUS_VERIFIED:
        normalized["status"] = EXAMPLE_STATUS_CONVERSION_VERIFIED
    elif status not in {EXAMPLE_STATUS_CANDIDATE, EXAMPLE_STATUS_CONVERSION_VERIFIED}:
        raise core.WorkflowError(f"example record has an unsupported status: {status}")
    lifecycle = normalized.get("lifecycle")
    if lifecycle is None:
        lifecycle = {"status": EXAMPLE_LIFECYCLE_ACTIVE}
        normalized["lifecycle"] = lifecycle
    if not isinstance(lifecycle, dict):
        raise core.WorkflowError("example lifecycle must be an object")
    lifecycle_status = lifecycle.get("status")
    if lifecycle_status not in {EXAMPLE_LIFECYCLE_ACTIVE, EXAMPLE_LIFECYCLE_REVOKED}:
        raise core.WorkflowError("example lifecycle.status must be active or revoked")
    if lifecycle_status == EXAMPLE_LIFECYCLE_REVOKED:
        for field in ("reason", "changedAt"):
            if not isinstance(lifecycle.get(field), str) or not lifecycle[field].strip():
                raise core.WorkflowError(f"revoked example lifecycle requires {field}")
        try:
            changed_at = datetime.fromisoformat(
                lifecycle["changedAt"].replace("Z", "+00:00")
            )
        except ValueError as error:
            raise core.WorkflowError(
                "example lifecycle.changedAt must be an ISO-8601 timestamp"
            ) from error
        if changed_at.tzinfo is None:
            raise core.WorkflowError("example lifecycle.changedAt must include a timezone")
    for field in ("reference", "supersededBy"):
        if field in lifecycle and (
            not isinstance(lifecycle[field], str) or not lifecycle[field].strip()
        ):
            raise core.WorkflowError(f"example lifecycle.{field} must be a non-empty string")
    superseded_by = lifecycle.get("supersededBy")
    if superseded_by and not re.fullmatch(
        r"model-conversion/[A-Za-z0-9][A-Za-z0-9._-]+", superseded_by
    ):
        raise core.WorkflowError("example lifecycle.supersededBy must be an example id")
    normalized.setdefault("knownLimits", list(CONVERSION_KNOWN_LIMITS))
    device_validation = normalized.get("deviceValidation")
    if device_validation is None:
        normalized["deviceValidation"] = {
            "status": DEVICE_VALIDATION_NONE["status"],
            "knownLimits": list(DEVICE_VALIDATION_NONE["knownLimits"]),
        }
    elif not isinstance(device_validation, dict):
        raise core.WorkflowError("example deviceValidation must be an object")
    else:
        device_status = device_validation.get("status")
        if device_status not in {"none", "referenced"}:
            raise core.WorkflowError("example deviceValidation.status must be none or referenced")
        if device_status == "referenced" and not isinstance(
            device_validation.get("reference"), str
        ):
            raise core.WorkflowError("referenced device validation requires a reference")
        limits = device_validation.get("knownLimits", [])
        if not isinstance(limits, list) or any(not isinstance(item, str) for item in limits):
            raise core.WorkflowError("example deviceValidation.knownLimits must be a string array")
    return normalized


def _example_is_active(record: dict[str, Any]) -> bool:
    return record.get("lifecycle", {}).get("status") == EXAMPLE_LIFECYCLE_ACTIVE


def _scope_summary_lines(
    *,
    status: str,
    validated_stages: list[str] | None = None,
    known_limits: list[str] | None = None,
    device_validation: dict[str, Any] | None = None,
    seal_code: str | None = None,
    lifecycle_status: str = "not-applicable",
) -> list[str]:
    stage_labels = {
        "onnx-preflight": "ONNX preflight",
        "transform": "transform",
        "deploy": "deploy",
        "model-info": "model-info",
        "tensor-compare": "tensor-compare",
    }
    covered = [stage_labels.get(item, item) for item in (validated_stages or [])]
    limits = known_limits or list(CONVERSION_KNOWN_LIMITS)
    normalized_limits = [
        re.sub(r"\s+(?:is|are) not covered$", "", item.strip(), flags=re.IGNORECASE)
        for item in limits
    ]
    device_status = str((device_validation or DEVICE_VALIDATION_NONE).get("status", "none"))
    return [
        f"STATUS: {status}",
        f"LIFECYCLE: {lifecycle_status}",
        "COVERS: " + (", ".join(covered) if covered else "none"),
        "NOT COVERED: " + "; ".join(normalized_limits),
        f"DEVICE VALIDATION: {device_status}",
        f"SEAL: {seal_code or 'none'}",
    ]


def _validated_conversion_stages(manifest: dict[str, Any]) -> list[str]:
    manifest_stages = manifest.get("stages", {})
    stage_keys = {
        "onnx-preflight": "onnxPreflight",
        "transform": "transform",
        "deploy": "deploy",
        "model-info": "modelInfo",
        "tensor-compare": "tensorCompare",
    }
    return [
        stage
        for stage in CONVERSION_COVERAGE
        if manifest_stages.get(stage_keys[stage], {}).get("status") == "PASS"
    ]


def _print_scope_summary(**kwargs: Any) -> None:
    for line in _scope_summary_lines(**kwargs):
        print(line)


def _device_evidence_record(raw_path: str, project_root: Path) -> dict[str, Any]:
    candidate = Path(raw_path).expanduser()
    if not candidate.is_absolute():
        candidate = project_root / candidate
    try:
        path = candidate.resolve(strict=True)
    except FileNotFoundError as error:
        raise core.WorkflowError(f"device evidence does not exist: {candidate}") from error
    if not path.is_file() or path.stat().st_size <= 0:
        raise core.WorkflowError("device evidence must be a non-empty file")
    sample = path.read_bytes()[:65536]
    try:
        text_sample = sample.decode("utf-8").strip()
    except UnicodeDecodeError:
        text_sample = ""
    if text_sample and re.fullmatch(
        r"(?:<[^>]+>|(?:TODO|TBD|placeholder)(?:[\s:._-].*)?)",
        text_sample,
        flags=re.IGNORECASE | re.DOTALL,
    ):
        raise core.WorkflowError("device evidence cannot be a placeholder")

    digest = core.sha256_file(path)
    reference = f"external:sha256:{digest}"
    if shutil.which("git") and core._is_relative_to(path, project_root.resolve()):
        relative = path.relative_to(project_root.resolve()).as_posix()
        tracked = subprocess.run(
            ["git", "-C", str(project_root), "ls-files", "--error-unmatch", "--", relative],
            text=True,
            capture_output=True,
            check=False,
        )
        commit = subprocess.run(
            ["git", "-C", str(project_root), "rev-parse", "HEAD"],
            text=True,
            capture_output=True,
            check=False,
        )
        commit_value = commit.stdout.strip()
        if tracked.returncode == 0 and re.fullmatch(r"[0-9a-f]{40}", commit_value):
            reference = f"git:{commit_value}:sha256:{digest}"
    return {
        "status": "referenced",
        "reference": reference,
        "knownLimits": [
            "the referenced device evidence is not embedded in this public example",
            "the conversion workflow does not independently revalidate device or business results",
        ],
    }


def _seal_chain_context(run_dir: Path) -> dict[str, Any]:
    contract_path = run_dir / "task-contract.json"
    route_path = run_dir / "route-assessment.json"
    environment_path = run_dir / "environment-report.json"
    manifest_path = run_dir / "execution-manifest.json"
    contract = core.validate_contract(core.load_json(contract_path))
    route = core.read_route_assessment(contract_path, run_dir, contract)
    environment = core.load_json(environment_path)
    if not isinstance(environment, dict):
        raise core.WorkflowError("environment-report.json must be an object")
    if (
        environment.get("runId") != contract["runId"]
        or environment.get("task") != contract["task"]
    ):
        raise core.WorkflowError("seal environment report does not belong to the task contract")
    if environment.get("contractSha256") != core.sha256_file(contract_path):
        raise core.WorkflowError("seal task contract and environment report do not match")
    if environment.get("routeAssessmentSha256") != core.sha256_file(route_path):
        raise core.WorkflowError("seal route assessment and environment report do not match")
    if environment.get("environmentVerdict") != "READY":
        raise core.WorkflowError("seal requires a READY doctor report")
    manifest = core.load_json(manifest_path)
    if not isinstance(manifest, dict) or manifest.get("status") != "COMPLETE":
        raise core.WorkflowError("seal requires a COMPLETE execution manifest")
    required_conversion_stages = (
        "onnxPreflight",
        "transform",
        "deploy",
        "modelInfo",
        "tensorCompare",
    )
    incomplete_stages = [
        stage
        for stage in required_conversion_stages
        if manifest.get("stages", {}).get(stage, {}).get("status") != "PASS"
    ]
    if incomplete_stages:
        raise core.WorkflowError(
            "seal requires PASS for conversion stages: " + ", ".join(incomplete_stages)
        )
    if manifest.get("contractSha256") != core.sha256_file(contract_path):
        raise core.WorkflowError("seal contract and execution manifest do not match")
    if manifest.get("routeAssessmentSha256") != core.sha256_file(route_path):
        raise core.WorkflowError("seal route assessment and execution manifest do not match")
    input_failures = _conversion_input_failures(manifest, conversion_parameters(contract, run_dir), run_dir)
    if input_failures:
        raise core.WorkflowError("; ".join(input_failures))
    compatibility = next(
        (
            item
            for item in environment.get("checks", [])
            if isinstance(item, dict) and item.get("id") == "compatibility-matrix"
        ),
        None,
    )
    if not compatibility or compatibility.get("status") != "PASS":
        raise core.WorkflowError("doctor compatibility-matrix must PASS before a seal is issued")
    toolchain_id = str(environment.get("toolchain", {}).get("id", ""))
    if not re.fullmatch(r"sha256:[0-9a-f]{64}", toolchain_id):
        raise core.WorkflowError("seal requires a frozen compiler toolchain digest")
    if manifest.get("toolchain", {}).get("id") != toolchain_id:
        raise core.WorkflowError("seal toolchain differs from the execution manifest")
    commit = str(environment.get("commit", ""))
    tree = str(environment.get("tree", ""))
    if not re.fullmatch(r"[0-9a-f]{40}", commit) or not re.fullmatch(r"[0-9a-f]{40}", tree):
        raise core.WorkflowError("seal requires frozen commit and tree identities")
    if manifest.get("commit") != commit or manifest.get("tree") != tree:
        raise core.WorkflowError("seal repository identity differs from the execution manifest")
    admitted_repository = environment.get("repository", {})
    repository = manifest.get("repository", {})
    if any(
        admitted_repository.get(field) != expected
        for field, expected in (("commit", commit), ("tree", tree))
    ):
        raise core.WorkflowError("seal repository snapshot differs from doctor commit or tree")
    if any(
        repository.get(field) != admitted_repository.get(field)
        for field in ("commit", "tree", "worktreeFingerprint")
    ):
        raise core.WorkflowError("seal execution repository differs from doctor admission")
    if any(
        snapshot.get("trackedChanges") != 0 or snapshot.get("untrackedFiles") != 0
        for snapshot in (admitted_repository, repository)
    ):
        raise core.WorkflowError("seal requires a clean recorded repository state")
    return {
        "runId": contract["runId"],
        "chainStatus": {
            "contract": "PASS",
            "assessment": "PASS" if route.get("routeVerdict") == "READY" else "FAIL",
            "doctor": "PASS",
            "conversion": "PASS",
        },
        "chainDigests": {
            "contract": core.sha256_file(contract_path),
            "assessment": core.sha256_file(route_path),
            "doctor": core.sha256_file(environment_path),
            "conversion": core.sha256_file(manifest_path),
        },
        "toolchainDigest": toolchain_id.removeprefix("sha256:")[:12],
        "commit": commit,
        "tree": tree,
    }


def _seal_code(payload: dict[str, Any]) -> str:
    canonical = json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return "CE1-" + hashlib.sha256(canonical).hexdigest()[:12]


def issue_verification_seal(
    run_dir: Path, evidence: dict[str, Any]
) -> tuple[dict[str, Any] | None, str]:
    if evidence.get("developmentVerdict") != "COMPLETE":
        return None, "conversion verification is not COMPLETE"
    try:
        context = _seal_chain_context(run_dir)
    except core.WorkflowError as error:
        return None, str(error)
    identity = {"sealVersion": SEAL_VERSION, **context}
    seal = {
        **identity,
        "issuedAt": utc_now(),
        "sealCode": _seal_code(identity),
    }
    core.atomic_write_json(run_dir / "seal.json", seal)
    return seal, ""


def validate_verification_seal(
    run_dir: Path,
) -> tuple[bool, str, dict[str, Any] | None]:
    seal_path = run_dir / "seal.json"
    if not seal_path.is_file():
        return False, "seal.json is missing", None
    try:
        seal = core.load_json(seal_path)
        if not isinstance(seal, dict):
            raise core.WorkflowError("seal.json must be an object")
        context = _seal_chain_context(run_dir)
    except core.WorkflowError as error:
        return False, str(error), None
    expected_identity = {"sealVersion": SEAL_VERSION, **context}
    if any(seal.get(key) != value for key, value in expected_identity.items()):
        return False, "seal identity no longer matches the current evidence chain", seal
    if not isinstance(seal.get("issuedAt"), str) or not seal["issuedAt"]:
        return False, "seal issuedAt is missing", seal
    if seal.get("sealCode") != _seal_code(expected_identity):
        return False, "seal short code is invalid", seal
    return True, "", seal


def _example_path(raw_path: str, project_root: Path = PROJECT_ROOT) -> Path:
    root = (project_root / "test" / "agent" / "examples" / "model-conversion").resolve()
    candidate = Path(raw_path).expanduser()
    if not candidate.is_absolute():
        candidate = project_root / candidate
    parent = candidate.parent.resolve()
    path = parent / candidate.name
    if not core._is_relative_to(path, root):
        raise core.WorkflowError("example record must stay under test/agent/examples/model-conversion/")
    if path.name in {"index.json", "schema.json"} or path.suffix != ".json":
        raise core.WorkflowError("example record needs a dedicated .json file")
    return path


def _recording_is_promotion_ready(
    record: dict[str, Any], manifest: dict[str, Any], parameters: dict[str, Any]
) -> bool:
    recordings = record.get("recordings", [])
    toolchain = manifest.get("toolchain", {})
    package = toolchain.get("package", {})
    tools = toolchain.get("tools", {})
    frozen_toolchain = bool(
        toolchain.get("kind") in core.TOOLCHAIN_KINDS
        and re.fullmatch(r"sha256:[0-9a-f]{64}", str(toolchain.get("id", "")))
        and package.get("name")
        and package.get("version")
        and re.fullmatch(r"[0-9a-f]{64}", str(package.get("recordSha256", "")))
        and all(
            re.fullmatch(r"[0-9a-f]{64}", str(tools.get(key, {}).get("sha256", "")))
            for key in ("modelTransform", "modelDeploy", "modelTool")
        )
        and (
            toolchain.get("kind") != "container-image"
            or toolchain.get("image", {}).get("id")
        )
    )
    return bool(
        parameters["targetChip"] == "bm1688"
        and parameters["quantization"] == "F16"
        and frozen_toolchain
        and parameters["sourceUrl"]
        and parameters["recordedBy"]
        and manifest["stages"].get("tensorCompare", {}).get("status") == "PASS"
        and manifest["stages"].get("modelInfo", {}).get("status") == "PASS"
        and len(recordings) >= 2
        and all(item.get("commit") and item.get("tree") for item in recordings)
        and len({item.get("commit") for item in recordings}) == 1
        and len({item.get("tree") for item in recordings}) == 1
        and manifest.get("repository", {}).get("trackedChanges") == 0
        and manifest.get("repository", {}).get("untrackedFiles") == 0
    )


# Beta: promotion remains opt-in until the first sealed conversion-verified example is published.
def record_example(
    raw_path: str,
    manifest: dict[str, Any],
    parameters: dict[str, Any],
    *,
    run_dir: Path,
    project_root: Path = PROJECT_ROOT,
    device_evidence: str | None = None,
    emit_summary: bool = True,
) -> dict[str, Any]:
    path = _example_path(raw_path, project_root)
    if manifest.get("toolchain", {}).get("layout") == "source-tree":
        raise core.WorkflowError(
            "source-tree task conversion is supported; public example recording requires a "
            "compiler-source identity schema and is not enabled by this task route"
        )
    if not parameters["recordedBy"] or not parameters["sourceUrl"]:
        raise core.WorkflowError(
            "recording requires measured parameters.recordedBy and parameters.sourceUrl"
        )
    if core.redact_text(parameters["sourceUrl"]) != parameters["sourceUrl"]:
        raise core.WorkflowError("example sourceUrl must not contain credentials")
    bmodel = manifest["artifacts"][0]
    seal_valid, _, seal = validate_verification_seal(run_dir)
    recording = {
        "recordedBy": parameters["recordedBy"],
        "recordedAt": manifest["completedAt"],
        "commit": manifest.get("commit"),
        "tree": manifest.get("tree"),
        "manifestSha256": core.sha256_file(run_dir / "execution-manifest.json"),
        "artifactSha256": bmodel["sha256"],
        "artifactSizeBytes": bmodel["sizeBytes"],
        "tensorTolerance": manifest["stages"]["tensorCompare"].get("tolerance"),
        "tensorTolerancePolicy": manifest["stages"]["tensorCompare"].get(
            "tolerancePolicy", "contract-override"
        ),
    }
    if seal_valid and seal:
        recording["seal"] = seal["sealCode"]
    applicability = {
        "modelFamily": parameters["modelFamily"],
        "sourceFormat": "onnx",
        "targetBackend": parameters["targetBackend"],
        "targetChip": parameters["targetChip"],
        "inputLayout": parameters["inputLayout"],
        "quantization": parameters["quantization"],
        "inputShapes": parameters["inputShapes"],
        "expectedOutputShapes": parameters["expectedOutputShapes"],
    }
    source = {
        "model": parameters["sourceModel"].name,
        "url": parameters["sourceUrl"],
        "sha256": manifest["source"]["sha256"],
    }
    manifest_toolchain = manifest["toolchain"]
    toolchain = {
        "kind": manifest_toolchain["kind"],
        "id": manifest_toolchain["id"],
        "package": {
            key: manifest_toolchain["package"].get(key)
            for key in ("name", "version", "recordSha256")
        },
        "tools": {
            key: {"sha256": manifest_toolchain["tools"][key].get("sha256")}
            for key in ("modelTransform", "modelDeploy", "modelTool")
        },
    }
    if manifest_toolchain.get("image"):
        toolchain["image"] = {
            key: manifest_toolchain["image"].get(key)
            for key in ("reference", "id", "repoDigests")
        }
    if path.exists():
        record = _normalize_example_record(core.load_json(path))
        if not _example_is_active(record):
            raise core.WorkflowError(
                "revoked examples cannot receive new recordings; use a new example id"
            )
        for field, expected in (
            ("applicability", applicability),
            ("source", source),
            ("toolchain", toolchain),
        ):
            if record.get(field) != expected:
                raise core.WorkflowError(
                    f"recording does not match the existing example {field}; use a new example id"
                )
        if any(item.get("manifestSha256") == recording["manifestSha256"] for item in record["recordings"]):
            raise core.WorkflowError("this execution manifest is already recorded")
        record["recordings"].append(recording)
    else:
        record = {
            "schemaVersion": "1.0",
            "exampleId": f"model-conversion/{path.stem}",
            "task": "model-conversion",
            "status": EXAMPLE_STATUS_CANDIDATE,
            "lifecycle": {"status": EXAMPLE_LIFECYCLE_ACTIVE},
            "applicability": applicability,
            "source": source,
            "toolchain": toolchain,
            "validatedStages": _validated_conversion_stages(manifest),
            "recordings": [recording],
            "knownLimits": list(CONVERSION_KNOWN_LIMITS),
            "deviceValidation": {
                "status": DEVICE_VALIDATION_NONE["status"],
                "knownLimits": list(DEVICE_VALIDATION_NONE["knownLimits"]),
            },
        }
    if device_evidence:
        record["deviceValidation"] = _device_evidence_record(device_evidence, project_root)
    promotion_ready = _recording_is_promotion_ready(record, manifest, parameters)
    record["status"] = (
        EXAMPLE_STATUS_CONVERSION_VERIFIED
        if promotion_ready and seal_valid
        else EXAMPLE_STATUS_CANDIDATE
    )
    if record["status"] == EXAMPLE_STATUS_CONVERSION_VERIFIED and seal:
        record["seal"] = seal["sealCode"]
    else:
        record.pop("seal", None)
    serialized = json.dumps(record, ensure_ascii=False)
    if re.search(r"<[^>]+>|\b(?:TODO|TBD|placeholder)\b", serialized, flags=re.IGNORECASE):
        raise core.WorkflowError("example records cannot contain placeholders")
    core.atomic_write_json(path, record)
    path.chmod(0o644)
    if emit_summary:
        _print_scope_summary(
            status=record["status"],
            validated_stages=record.get("validatedStages", []),
            known_limits=record.get("knownLimits", []),
            device_validation=record.get("deviceValidation"),
            seal_code=seal.get("sealCode") if seal_valid and seal else None,
            lifecycle_status=record["lifecycle"]["status"],
        )
    return record


def _resolve_manifest_artifact(run_dir: Path, entry: dict[str, Any]) -> Path:
    raw_path = _require_string(entry.get("path"), "manifest artifact path")
    return core.resolve_run_input(run_dir, raw_path)


def _package_stage(contract: dict[str, Any], run_dir: Path, parameters: dict[str, Any]) -> dict[str, Any]:
    output_kind = str(parameters["raw"].get("outputKind", "bmodel"))
    if output_kind != "model-package":
        return {"id": "S4", "status": "SKIP", "detail": "The task requests a bmodel, not a full import package."}
    package_raw = parameters["raw"].get("packageDirectory")
    if not isinstance(package_raw, str) or not package_raw:
        return {
            "id": "S4",
            "status": "UNVERIFIED",
            "detail": "A full package was requested but parameters.packageDirectory is not recorded.",
        }
    candidate = Path(package_raw)
    if not candidate.is_absolute():
        candidate = run_dir / candidate
    try:
        package = candidate.resolve(strict=True)
    except FileNotFoundError:
        return {"id": "S4", "status": "FAIL", "detail": "The requested model package directory does not exist."}
    if not core._is_relative_to(package, run_dir.resolve()) or not package.is_dir():
        return {"id": "S4", "status": "FAIL", "detail": "The package directory escapes the current run."}
    if not re.fullmatch(r"prod_[A-Z0-9]+_[0-9]{7}_.+_V[0-9]+\.0\.[0-9]+", package.name):
        return {"id": "S4", "status": "FAIL", "detail": "The package directory name does not match the import contract."}
    config_path = package / "config.json"
    model_path = package / "model.nn"
    if not config_path.is_file() or not model_path.is_file():
        return {"id": "S4", "status": "FAIL", "detail": "The package must contain config.json and model.nn."}
    try:
        config = core.load_json(config_path)
    except core.WorkflowError as error:
        return {"id": "S4", "status": "FAIL", "detail": str(error)}
    if str(config.get("chip_type", "")).lower() != parameters["targetChip"]:
        return {"id": "S4", "status": "FAIL", "detail": "config.json chip_type differs from the task contract."}
    models = config.get("models")
    if not isinstance(models, list) or not models:
        return {"id": "S4", "status": "FAIL", "detail": "config.json models must be non-empty."}
    for model in models:
        if not isinstance(model, dict) or not model.get("inputs") or not model.get("outputs"):
            return {"id": "S4", "status": "FAIL", "detail": "Each configured model needs inputs and outputs."}
    return {
        "id": "S4",
        "status": "PASS",
        "detail": "Package name, config.json, model.nn, chip, and tensor declarations are present and self-consistent.",
    }


def _example_applicable(example: dict[str, Any], parameters: dict[str, Any]) -> bool:
    applicability = example.get("applicability", {})
    expected = {
        "modelFamily": parameters["modelFamily"],
        "sourceFormat": "onnx",
        "targetBackend": parameters["targetBackend"],
        "targetChip": parameters["targetChip"],
        "inputLayout": parameters["inputLayout"],
        "quantization": parameters["quantization"],
        "inputShapes": parameters["inputShapes"],
        "expectedOutputShapes": parameters["expectedOutputShapes"],
    }
    return all(applicability.get(key) == value for key, value in expected.items())


def _accepted_waiver(contract: dict[str, Any], stage: str) -> dict[str, Any] | None:
    waivers = contract.get("acceptance", {}).get("waivers", [])
    if not isinstance(waivers, list):
        raise core.WorkflowError("acceptance.waivers must be an array")
    for waiver in waivers:
        if not isinstance(waiver, dict) or waiver.get("stage") != stage:
            continue
        reason = waiver.get("reason")
        if not isinstance(reason, str) or not reason.strip() or waiver.get("confirmedByUser") is not True:
            raise core.WorkflowError(
                f"waiver for {stage} requires a reason and confirmedByUser=true"
            )
        return {
            "stage": stage,
            "reason": core.redact_text(reason.strip()),
            "confirmedByUser": True,
        }
    return None


def verify_conversion(
    contract_path: Path,
    run_dir: Path,
    contract: dict[str, Any],
    *,
    example_path: str | None = None,
    project_root: Path = PROJECT_ROOT,
) -> dict[str, Any]:
    parameters = conversion_parameters(contract, run_dir)
    environment = _read_environment_report(contract_path, run_dir, contract)
    selection = _read_asset_selection(run_dir)
    manifest_path = run_dir / "execution-manifest.json"
    manifest = core.load_json(manifest_path)
    if not isinstance(manifest, dict) or manifest.get("status") != "COMPLETE":
        raise core.WorkflowError("execution-manifest.json is absent or not COMPLETE")
    if (
        manifest.get("runId") != contract["runId"]
        or manifest.get("contractSha256") != core.sha256_file(contract_path)
    ):
        raise core.WorkflowError("execution manifest does not match the current contract")
    if environment.get("toolchain", {}).get("layout") == "source-tree":
        current, error = core.inspect_toolchain(parameters["toolchainSpec"])
        expected_id = environment["toolchain"]["id"]
        if not current or current.get("id") != expected_id or manifest.get("toolchain", {}).get("id") != expected_id:
            raise core.WorkflowError("source compiler identity changed before verification; rerun doctor: " + error)

    stages: list[dict[str, Any]] = []
    preflight = manifest.get("stages", {}).get("onnxPreflight", {})
    try:
        preflight_path = core.resolve_run_input(run_dir, str(preflight.get("report", "")))
    except core.WorkflowError:
        preflight_path = None
    if (
        preflight.get("status") == "PASS"
        and preflight_path is not None
        and core.sha256_file(preflight_path) == preflight.get("reportSha256")
    ):
        preflight_data = core.load_json(preflight_path)
        current_source_sha256 = core.sha256_file(parameters["sourceModel"])
        source_matches = (
            preflight_data.get("model", {}).get("sha256")
            == manifest["source"]["sha256"]
            == current_source_sha256
        )
        stages.append(
            {
                "id": "S1",
                "status": "PASS" if source_matches else "FAIL",
                "detail": (
                    "ONNX checker and ONNX Runtime smoke passed for the recorded source."
                    if source_matches
                    else "ONNX preflight source identity differs from the execution manifest."
                ),
            }
        )
    else:
        stages.append({"id": "S1", "status": "FAIL", "detail": "ONNX preflight evidence is missing or changed."})

    artifact_entries = manifest.get("artifacts", [])
    artifact_failures = _conversion_input_failures(manifest, parameters, run_dir)
    deliverables = []
    for entry in artifact_entries if isinstance(artifact_entries, list) else []:
        try:
            path = _resolve_manifest_artifact(run_dir, entry)
        except core.WorkflowError as error:
            artifact_failures.append(str(error))
            continue
        if core.sha256_file(path) != entry.get("sha256"):
            artifact_failures.append(f"SHA-256 mismatch for {entry.get('path')}")
            continue
        deliverables.append(
            {
                "path": entry["path"],
                "sha256": entry["sha256"],
                "sizeBytes": path.stat().st_size,
                "role": entry.get("role", "artifact"),
            }
        )
    model_info = manifest.get("stages", {}).get("modelInfo", {})
    if artifact_failures or not deliverables:
        stages.append(
            {
                "id": "S2",
                "status": "FAIL",
                "detail": "; ".join(artifact_failures) or "No conversion artifact is recorded.",
            }
        )
    elif model_info.get("status") == "PASS" and model_info.get("contractMatches") is True:
        stages.append(
            {
                "id": "S2",
                "status": "PASS",
                "detail": "Artifact identity and model_tool input/output contract match the candidate task.",
            }
        )
    else:
        stages.append(
            {
                "id": "S2",
                "status": "UNVERIFIED",
                "detail": "Artifact hashes pass, but model_tool input/output matching is not fully verified.",
            }
        )

    tensor = manifest.get("stages", {}).get("tensorCompare", {})
    tensor_status = tensor.get("status") if tensor.get("status") in {"PASS", "FAIL", "UNVERIFIED"} else "UNVERIFIED"
    previous_attempts = manifest.get("previousAttempts", [])
    if not isinstance(previous_attempts, list):
        raise core.WorkflowError("execution manifest previousAttempts must be an array")
    failed_tensor_attempts = [
        item.get("attempt")
        for item in previous_attempts
        if isinstance(item, dict)
        and item.get("stageStatuses", {}).get("tensorCompare") == "FAIL"
    ]
    tensor_waiver = _accepted_waiver(contract, "tensor-compare")
    tensor_detail = str(tensor.get("detail", "Tensor comparison evidence is unavailable."))
    if tensor_status == "FAIL":
        if tensor_waiver:
            tensor_detail += " A waiver does not supersede a measured failure."
    elif tensor_status != "PASS" and failed_tensor_attempts and not tensor_waiver:
        tensor_status = "FAIL"
        tensor_detail = (
            "A previous tensor comparison failed in attempt(s) "
            + ", ".join(str(value) for value in failed_tensor_attempts)
            + "; the current attempt did not supersede it with a pass."
        )
    elif tensor_status == "UNVERIFIED" and tensor_waiver:
        tensor_status = "SKIP"
        tensor_detail = "User-confirmed waiver: " + tensor_waiver["reason"]
    stages.append(
        {
            "id": "S3",
            "status": tensor_status,
            "detail": tensor_detail,
        }
    )
    stages.append(_package_stage(contract, run_dir, parameters))
    stages.append(
        {
            "id": "S5",
            "status": "UNVERIFIED",
            "detail": "Device import, runtime, and business accuracy were not run by this local workflow.",
        }
    )

    required_tensor = bool(contract["acceptance"].get("requireTensorCompare", True)) and not tensor_waiver
    required_package = str(parameters["raw"].get("outputKind", "bmodel")) == "model-package"
    by_id = {stage["id"]: stage for stage in stages}
    if any(by_id[item]["status"] == "FAIL" for item in ("S1", "S2")):
        development_verdict = "FAILED"
    elif required_package and by_id["S4"]["status"] == "FAIL":
        development_verdict = "FAILED"
    elif (
        by_id["S1"]["status"] == "PASS"
        and by_id["S2"]["status"] == "PASS"
        and (not required_tensor or by_id["S3"]["status"] == "PASS")
        and (not required_package or by_id["S4"]["status"] == "PASS")
    ):
        development_verdict = "COMPLETE"
    elif deliverables:
        development_verdict = "PARTIAL"
    else:
        development_verdict = "UNVERIFIED"

    promotion_requested = bool(contract["acceptance"].get("promoteExample", False) or example_path)
    promotion_verdict = "NOT_REQUESTED"
    selected_example = None
    selected_example_lifecycle = None
    if promotion_requested:
        promotion_verdict = "NOT_READY"
        if example_path:
            path = _example_path(example_path, project_root)
            if not path.is_file():
                raise core.WorkflowError(f"example record does not exist: {path}")
            selected_example = _normalize_example_record(core.load_json(path))
            selected_example_lifecycle = selected_example["lifecycle"]["status"]
            seal_valid, _, seal = validate_verification_seal(run_dir)
            if (
                selected_example.get("status") == EXAMPLE_STATUS_CONVERSION_VERIFIED
                and _example_is_active(selected_example)
                and seal_valid
                and seal is not None
                and selected_example.get("seal") == seal.get("sealCode")
                and _example_applicable(selected_example, parameters)
                and all(by_id[item]["status"] == "PASS" for item in ("S1", "S2", "S3"))
                and any(
                    recording.get("manifestSha256") == core.sha256_file(manifest_path)
                    for recording in selected_example.get("recordings", [])
                )
            ):
                promotion_verdict = "READY"

    commands = [core.redact_text(str(value)) for value in manifest.get("commands", [])]
    evidence = {
        "schemaVersion": "1.0",
        "runId": contract["runId"],
        "task": contract["task"],
        "userObjective": core.redact_text(contract["userObjective"]),
        "commit": environment.get("commit"),
        "timestamp": utc_now(),
        "environmentReport": "environment-report.json",
        "routeAssessment": "route-assessment.json",
        "routeAssessmentSha256": environment["routeAssessmentSha256"],
        "contractSha256": core.sha256_file(contract_path),
        "authorityGrants": sorted(core._authority_grants(contract)),
        "selectedAssets": core.redact_data(selection["selectedAssets"]),
        "assetDifferences": core.redact_data(selection["differences"]),
        "selectedExample": selected_example.get("exampleId") if selected_example else None,
        "selectedExampleLifecycle": selected_example_lifecycle,
        "attempts": [*previous_attempts, _attempt_summary(manifest)],
        "conversionInputs": manifest.get("conversionInputs", []),
        "dataFlow": core.redact_data(manifest.get("dataFlow", {"status": "UNVERIFIED"})),
        "waivers": [tensor_waiver] if tensor_waiver else [],
        "stages": stages,
        "deliverables": deliverables,
        "developmentVerdict": development_verdict,
        "promotionVerdict": promotion_verdict,
        "deviceVerdict": "NOT_RUN",
        "pendingOnDevice": [
            "model import",
            "image validation",
            "video pipeline",
            "offline business-accuracy comparison",
        ],
        "commands": commands,
    }
    core.atomic_write_json(run_dir / "evidence.json", evidence)
    lines = [
        "# Model conversion evidence",
        "",
        f"- Task result: **{development_verdict}**",
        f"- Official-example promotion: **{promotion_verdict}**",
        "- Device result: **NOT_RUN**",
        "",
        "## Layered verification",
        "",
    ]
    if selected_example_lifecycle:
        lines.insert(
            5,
            f"- Selected-example lifecycle: **{selected_example_lifecycle.upper()}**",
        )
    lines.extend(f"- {stage['id']} {stage['status']}: {stage['detail']}" for stage in stages)
    lines.extend(
        [
            "",
            "## Attempt and data-flow record",
            "",
            f"- Current attempt: {manifest.get('attempt', 1)}",
            f"- Data flow: {evidence['dataFlow'].get('mode', 'UNVERIFIED')} / {evidence['dataFlow'].get('status', 'UNVERIFIED')}",
            "",
            "## Deliverables",
            "",
            *[
                f"- `{item['path']}` — SHA-256 `{item['sha256']}` ({item['sizeBytes']} bytes)"
                for item in deliverables
            ],
            "",
            "Local conversion evidence does not imply device import, runtime, or production acceptance.",
            "",
        ]
    )
    _write_private_text(run_dir / "evidence.md", "\n".join(lines))
    return evidence


def convert_main(arguments: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="convert_model.sh")
    parser.add_argument("--contract", required=True)
    parser.add_argument("--test-input")
    parser.add_argument("--record-example")
    parser.add_argument("--device-evidence")
    options = parser.parse_args(arguments)
    if options.device_evidence and not options.record_example:
        raise core.WorkflowError("--device-evidence requires --record-example")
    contract_path, run_dir, contract = core.resolve_contract_context(options.contract)
    try:
        manifest, parameters = execute_conversion(
            contract_path,
            run_dir,
            contract,
            test_input_override=options.test_input,
        )
        if options.record_example:
            record = record_example(
                options.record_example,
                manifest,
                parameters,
                run_dir=run_dir,
                device_evidence=options.device_evidence,
            )
            print(f"Example recording status: {record['status']}")
        print(f"Conversion artifact: {manifest['artifacts'][0]['path']}")
        print(f"Execution manifest: {_run_relative(run_dir / 'execution-manifest.json', run_dir)}")
        return 0
    except ExecutionFailure as error:
        print(f"conversion failed: {error}", file=sys.stderr)
        return 1


def verify_main(arguments: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="verify.sh")
    parser.add_argument("--contract", required=True)
    parser.add_argument("--example")
    parser.add_argument("--record-example")
    parser.add_argument("--device-evidence")
    options = parser.parse_args(arguments)
    if options.device_evidence and not options.record_example:
        raise core.WorkflowError("--device-evidence requires --record-example")
    if options.example and options.record_example:
        raise core.WorkflowError("use either --example or --record-example, not both")
    contract_path, run_dir, contract = core.resolve_contract_context(options.contract)
    evidence = verify_conversion(
        contract_path,
        run_dir,
        contract,
        example_path=options.example,
    )
    manifest = core.load_json(run_dir / "execution-manifest.json")
    seal, seal_reason = issue_verification_seal(run_dir, evidence)
    record = None
    if options.record_example:
        parameters = conversion_parameters(contract, run_dir)
        record = record_example(
            options.record_example,
            manifest,
            parameters,
            run_dir=run_dir,
            device_evidence=options.device_evidence,
            emit_summary=False,
        )
        previous_promotion = evidence["promotionVerdict"]
        evidence["selectedExample"] = record["exampleId"]
        evidence["selectedExampleLifecycle"] = record["lifecycle"]["status"]
        evidence["promotionVerdict"] = (
            "READY"
            if record["status"] == EXAMPLE_STATUS_CONVERSION_VERIFIED
            and _example_is_active(record)
            else "NOT_READY"
        )
        core.atomic_write_json(run_dir / "evidence.json", evidence)
        evidence_markdown = run_dir / "evidence.md"
        rendered = evidence_markdown.read_text(encoding="utf-8").replace(
            f"- Official-example promotion: **{previous_promotion}**",
            f"- Official-example promotion: **{evidence['promotionVerdict']}**",
            1,
        )
        rendered = rendered.replace(
            "- Device result: **NOT_RUN**",
            (
                "- Selected-example lifecycle: "
                f"**{record['lifecycle']['status'].upper()}**\n"
                "- Device result: **NOT_RUN**"
            ),
            1,
        )
        _write_private_text(evidence_markdown, rendered)

    summary_status = (
        record["status"]
        if record
        else (
            EXAMPLE_STATUS_CONVERSION_VERIFIED
            if evidence["developmentVerdict"] == "COMPLETE"
            else EXAMPLE_STATUS_CANDIDATE
        )
    )
    _print_scope_summary(
        status=summary_status,
        validated_stages=_validated_conversion_stages(manifest),
        known_limits=list(CONVERSION_KNOWN_LIMITS),
        device_validation=(record or {}).get("deviceValidation", DEVICE_VALIDATION_NONE),
        seal_code=seal.get("sealCode") if seal else None,
        lifecycle_status=(
            record["lifecycle"]["status"]
            if record
            else evidence.get("selectedExampleLifecycle") or "not-applicable"
        ),
    )
    if seal:
        print("Seal file: seal.json")
    else:
        print(f"Seal issuance: NOT_ISSUED ({seal_reason})")
    print(f"Development verdict: {evidence['developmentVerdict']}")
    print(f"Promotion verdict: {evidence['promotionVerdict']}")
    print("Evidence: evidence.md")
    return 0 if evidence["developmentVerdict"] == "COMPLETE" else 1


def main(arguments: list[str] | None = None) -> int:
    args = list(sys.argv[1:] if arguments is None else arguments)
    if not args:
        print("usage: model_conversion_workflow.py convert|verify ...", file=sys.stderr)
        return 2
    command = args.pop(0)
    try:
        if command == "convert":
            return convert_main(args)
        if command == "verify":
            return verify_main(args)
        raise core.WorkflowError(f"unknown command: {command}")
    except core.WorkflowError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    os.umask(0o077)
    raise SystemExit(main())
