#!/usr/bin/env python3
"""Shared parameter, admission and evidence helpers for model conversion."""

from __future__ import annotations

import re
import shlex
import subprocess
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import agent_workflow as core


MODEL_NAME_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$")
TOLERANCE_PATTERN = re.compile(r"^(?:0(?:\.\d+)?|1(?:\.0+)?),(?:0(?:\.\d+)?|1(?:\.0+)?)$")


class ExecutionFailure(RuntimeError):
    """Raised when a measured conversion command fails."""


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def _require_string(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise core.WorkflowError(f"{field} must be a non-empty string")
    return value.strip()


def _validate_shapes(value: Any, field: str) -> list[list[int]]:
    if (
        not isinstance(value, list)
        or not value
        or any(
            not isinstance(shape, list)
            or not shape
            or any(not isinstance(dimension, int) or dimension <= 0 for dimension in shape)
            for shape in value
        )
    ):
        raise core.WorkflowError(f"{field} must be an array of positive-integer shape arrays")
    return value


def conversion_parameters(contract: dict[str, Any], run_dir: Path) -> dict[str, Any]:
    if contract["task"] != "model-conversion":
        raise core.WorkflowError("model conversion only supports task=model-conversion")
    params = contract.get("parameters", {})
    source_model = core.resolve_run_input(
        run_dir, _require_string(params.get("sourceModel"), "parameters.sourceModel")
    )
    if source_model.suffix.lower() != ".onnx":
        raise core.WorkflowError("the first conversion profile requires an ONNX source model")
    model_name = _require_string(params.get("modelName"), "parameters.modelName")
    if not MODEL_NAME_PATTERN.fullmatch(model_name):
        raise core.WorkflowError("parameters.modelName contains unsupported characters")
    target_backend = _require_string(params.get("targetBackend"), "parameters.targetBackend")
    target_chip = _require_string(params.get("targetChip"), "parameters.targetChip").lower()
    toolchain_chip = str(params.get("toolchainChip", target_chip)).strip().lower()
    quantization = _require_string(params.get("quantization"), "parameters.quantization").upper()
    input_layout = _require_string(params.get("inputLayout"), "parameters.inputLayout").upper()
    pixel_format = _require_string(params.get("pixelFormat"), "parameters.pixelFormat").lower()
    input_shapes = _validate_shapes(params.get("inputShapes"), "parameters.inputShapes")
    output_shapes = params.get("expectedOutputShapes", [])
    if output_shapes:
        output_shapes = _validate_shapes(output_shapes, "parameters.expectedOutputShapes")
    toolchain_spec, toolchain_error = core._toolchain_spec(params)
    if not toolchain_spec:
        raise core.WorkflowError(toolchain_error)
    preflight = params.get("preflight", {})
    if not isinstance(preflight, dict):
        raise core.WorkflowError("parameters.preflight must be an object")
    tolerance = params.get("tensorTolerance")
    if tolerance is not None:
        tolerance = _require_string(tolerance, "parameters.tensorTolerance")
        if not TOLERANCE_PATTERN.fullmatch(tolerance):
            raise core.WorkflowError("parameters.tensorTolerance must look like 0.99,0.90")
    return {
        "sourceModel": source_model,
        "modelName": model_name,
        "targetBackend": target_backend,
        "targetChip": target_chip,
        "toolchainChip": toolchain_chip,
        "quantization": quantization,
        "inputLayout": input_layout,
        "pixelFormat": pixel_format,
        "inputShapes": input_shapes,
        "expectedOutputShapes": output_shapes,
        "toolchainSpec": toolchain_spec,
        "tensorTolerance": tolerance,
        "pythonExecutable": str(
            preflight.get(
                "pythonExecutable",
                params.get("pythonExecutable", "python3"),
            )
        ),
        "modelFamily": str(params.get("modelFamily", "unspecified")),
        "sourceUrl": str(params.get("sourceUrl", "")),
        "recordedBy": str(params.get("recordedBy", "")),
        "raw": params,
    }


def _read_environment_report(
    contract_path: Path, run_dir: Path, contract: dict[str, Any]
) -> dict[str, Any]:
    core.read_route_assessment(contract_path, run_dir, contract)
    route_sha256 = core.sha256_file(run_dir / "route-assessment.json")
    report_path = run_dir / "environment-report.json"
    report = core.load_json(report_path)
    if not isinstance(report, dict):
        raise core.WorkflowError("environment-report.json must be an object")
    if report.get("runId") != contract["runId"] or report.get("task") != contract["task"]:
        raise core.WorkflowError("environment report does not belong to this task contract")
    if report.get("contractSha256") != core.sha256_file(contract_path):
        raise core.WorkflowError("task contract changed after environment admission; rerun doctor")
    if report.get("routeAssessmentSha256") != route_sha256:
        raise core.WorkflowError("route assessment changed after environment admission; rerun doctor")
    if report.get("environmentVerdict") != "READY":
        raise core.WorkflowError("environment admission is not READY; conversion must not start")
    admitted_repository = report.get("repository")
    if not isinstance(admitted_repository, dict):
        raise core.WorkflowError("environment report does not freeze repository working state; rerun doctor")
    current_repository = core._git_snapshot()
    for field in ("commit", "tree", "worktreeFingerprint"):
        if admitted_repository.get(field) != current_repository.get(field):
            raise core.WorkflowError(
                "repository state changed after environment admission; rerun doctor"
            )
    toolchain = report.get("toolchain")
    if not isinstance(toolchain, dict) or not toolchain.get("id"):
        raise core.WorkflowError("environment report does not freeze a complete toolchain identity")
    return report


def _read_asset_selection(run_dir: Path) -> dict[str, Any]:
    path = run_dir / "asset-selection.json"
    data = core.load_json(path)
    if not isinstance(data, dict):
        raise core.WorkflowError("asset-selection.json must be an object")
    selected = data.get("selectedAssets")
    differences = data.get("differences")
    if (
        not isinstance(selected, list)
        or not selected
        or any(not isinstance(item, str) or not item.strip() for item in selected)
    ):
        raise core.WorkflowError("asset-selection.json must list at least one selected asset")
    if not isinstance(differences, list) or any(not isinstance(item, str) for item in differences):
        raise core.WorkflowError("asset-selection.json differences must be a string array")
    return data


def _run_relative(path: Path, run_dir: Path) -> str:
    return path.resolve().relative_to(run_dir.resolve()).as_posix()


def _command_text(command: list[str], run_dir: Path) -> str:
    text = core.redact_text(shlex.join(command))
    return text.replace(str(run_dir.resolve()), "$RUN_DIR")


def _write_private_text(path: Path, value: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    path.write_text(value, encoding="utf-8")
    path.chmod(0o600)


def _run_logged(
    command: list[str],
    *,
    cwd: Path,
    log_path: Path,
    commands: list[str],
    run_dir: Path,
    timeout: int = 1800,
    environment: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    commands.append(_command_text(command, run_dir))
    try:
        process = subprocess.run(
            command,
            cwd=cwd,
            text=True,
            capture_output=True,
            check=False,
            timeout=timeout,
            env=environment,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        process = subprocess.CompletedProcess(command, 127, "", str(error))
    log = (
        f"$ {_command_text(command, run_dir)}\n"
        f"exitCode: {process.returncode}\n\n"
        f"stdout:\n{core.redact_text(process.stdout)}\n\n"
        f"stderr:\n{core.redact_text(process.stderr)}\n"
    )
    _write_private_text(log_path, log)
    return process


def _artifact(path: Path, run_dir: Path, role: str) -> dict[str, Any]:
    return {
        "path": _run_relative(path, run_dir),
        "sha256": core.sha256_file(path),
        "sizeBytes": path.stat().st_size,
        "role": role,
    }


def _failed_manifest(
    manifest_path: Path,
    manifest: dict[str, Any],
    detail: str,
) -> None:
    manifest["status"] = "FAILED"
    manifest["completedAt"] = utc_now()
    manifest["failure"] = core.redact_text(detail)
    core.atomic_write_json(manifest_path, manifest)


def _attempt_summary(
    manifest: dict[str, Any],
    *,
    archive_path: Path | None = None,
    run_dir: Path | None = None,
) -> dict[str, Any]:
    stages = manifest.get("stages", {})
    artifacts = manifest.get("artifacts", [])
    first_artifact = artifacts[0] if isinstance(artifacts, list) and artifacts else {}
    summary = {
        "attempt": int(manifest.get("attempt", 1)),
        "status": str(manifest.get("status", "UNKNOWN")),
        "startedAt": manifest.get("startedAt"),
        "completedAt": manifest.get("completedAt"),
        "failure": core.redact_text(str(manifest.get("failure", ""))) or None,
        "stageStatuses": {
            key: value.get("status", "UNKNOWN")
            for key, value in stages.items()
            if isinstance(value, dict)
        },
        "artifactSha256": first_artifact.get("sha256") if isinstance(first_artifact, dict) else None,
    }
    if archive_path is not None:
        summary["archive"] = (
            _run_relative(archive_path, run_dir)
            if run_dir is not None
            else str(archive_path)
        )
        summary["manifestSha256"] = core.sha256_file(archive_path)
    return summary


def _archive_previous_attempt(run_dir: Path) -> tuple[int, list[dict[str, Any]]]:
    manifest_path = run_dir / "execution-manifest.json"
    attempts_dir = run_dir / "attempts"
    attempts_dir.mkdir(parents=True, exist_ok=True, mode=0o700)
    archived: list[dict[str, Any]] = []
    maximum = 0
    for path in sorted(attempts_dir.glob("attempt-*.json")):
        match = re.fullmatch(r"attempt-(\d+)\.json", path.name)
        if not match:
            continue
        maximum = max(maximum, int(match.group(1)))
        data = core.load_json(path)
        if isinstance(data, dict):
            archived.append(_attempt_summary(data, archive_path=path, run_dir=run_dir))
    if manifest_path.is_file():
        current = core.load_json(manifest_path)
        if not isinstance(current, dict):
            raise core.WorkflowError("existing execution-manifest.json must be an object")
        attempt = int(current.get("attempt", maximum + 1))
        if attempt <= maximum or (attempts_dir / f"attempt-{attempt}.json").exists():
            attempt = maximum + 1
        current["attempt"] = attempt
        archive_path = attempts_dir / f"attempt-{attempt}.json"
        core.atomic_write_json(archive_path, current)
        archived.append(_attempt_summary(current, archive_path=archive_path, run_dir=run_dir))
        maximum = max(maximum, attempt)
    archived.sort(key=lambda item: item["attempt"])
    return maximum + 1, archived


def _data_flow_record(contract: dict[str, Any]) -> dict[str, Any]:
    parameters = contract.get("parameters", {})
    declared = parameters.get("dataFlow", {})
    if declared is None:
        declared = {}
    if not isinstance(declared, dict):
        raise core.WorkflowError("parameters.dataFlow must be an object")
    transfer_required = bool(parameters.get("requiresModelTransfer"))
    record = {
        "mode": "REMOTE_TRANSFER" if transfer_required else "LOCAL_ONLY",
        "status": "DECLARED" if transfer_required else "NOT_REQUIRED",
        "sourceZone": "current isolated run",
        "executionZone": "current isolated run",
        "evidenceReference": None,
        "credentialMaterialStored": False,
    }
    for key in ("status", "sourceZone", "executionZone", "evidenceReference"):
        value = declared.get(key)
        if value is not None:
            record[key] = core.redact_text(str(value))
    if record["status"] not in {"NOT_REQUIRED", "DECLARED", "COMPLETED", "FAILED"}:
        raise core.WorkflowError(
            "parameters.dataFlow.status must be NOT_REQUIRED, DECLARED, COMPLETED, or FAILED"
        )
    return record
