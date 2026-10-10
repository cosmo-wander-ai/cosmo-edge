#!/usr/bin/env python3
"""Measured ONNX-to-RKNN conversion and evidence workflow."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
import time
from pathlib import Path
from typing import Any

import agent_workflow as core
import conversion_common as common


PROJECT_ROOT = Path(__file__).resolve().parents[2]
VIDEO_SUFFIXES = {".mp4", ".mov", ".mkv", ".avi"}


def _repository_file(raw_value: Any, default: str, allowed_root: Path, field: str) -> Path:
    value = str(raw_value or default).strip()
    candidate = (PROJECT_ROOT / value).resolve()
    try:
        candidate.relative_to(allowed_root.resolve())
    except ValueError as error:
        raise core.WorkflowError(f"{field} must stay under {allowed_root.relative_to(PROJECT_ROOT)}") from error
    if not candidate.is_file():
        raise core.WorkflowError(f"{field} does not exist: {value}")
    return candidate


def _calibration_source(parameters: dict[str, Any], run_dir: Path) -> Path | None:
    if parameters["quantization"] != "INT8":
        return None
    raw = parameters["raw"].get("calibrationSource")
    if raw:
        return core.resolve_run_input(run_dir, str(raw))
    candidates = sorted(
        path for path in (run_dir / "inputs").iterdir() if path.suffix.lower() in VIDEO_SUFFIXES
    )
    if len(candidates) != 1:
        raise core.WorkflowError(
            "INT8 RKNN conversion requires parameters.calibrationSource or exactly one input video"
        )
    return candidates[0].resolve()


def conversion_parameters(contract: dict[str, Any], run_dir: Path) -> dict[str, Any]:
    parameters = common.conversion_parameters(contract, run_dir)
    if core._conversion_toolchain_family(parameters["targetChip"]) != "rknn":
        raise core.WorkflowError("RKNN conversion requires an RK/RV target chip")
    if str(parameters["raw"].get("outputKind", "")).lower() != "rknn":
        raise core.WorkflowError("RKNN conversion requires parameters.outputKind=rknn")
    if parameters["quantization"] not in {"INT8", "F16", "FP16"}:
        raise core.WorkflowError("RKNN conversion supports INT8 or FP16 in this workflow")

    model_name = parameters["modelName"]
    target_chip = parameters["targetChip"]
    spec_path = _repository_file(
        parameters["raw"].get("modelSpec"),
        f"config/rknn/models/{model_name}.json",
        PROJECT_ROOT / "config" / "rknn" / "models",
        "parameters.modelSpec",
    )
    profile_path = _repository_file(
        parameters["raw"].get("platformProfile"),
        f"config/rknn/platforms/{target_chip}.json",
        PROJECT_ROOT / "config" / "rknn" / "platforms",
        "parameters.platformProfile",
    )
    spec = core.load_json(spec_path)
    profile = core.load_json(profile_path)
    if not isinstance(spec, dict) or spec.get("name") != model_name:
        raise core.WorkflowError("RKNN model spec name does not match parameters.modelName")
    if not isinstance(profile, dict) or profile.get("backend") != "rknn":
        raise core.WorkflowError("RKNN platform profile must declare backend=rknn")
    if str(profile.get("chip", "")).lower() != target_chip:
        raise core.WorkflowError("RKNN platform profile chip does not match parameters.targetChip")
    if str(profile.get("conversion", {}).get("target_platform", "")).lower() != target_chip:
        raise core.WorkflowError("RKNN platform profile conversion target does not match the task")
    toolchain_lock_raw = profile.get("toolchain_lock")
    if not isinstance(toolchain_lock_raw, str) or not toolchain_lock_raw.strip():
        raise core.WorkflowError("RKNN platform profile must reference a toolchain lock")
    toolchain_lock_path = (profile_path.parent / toolchain_lock_raw).resolve()
    toolchain_lock_root = (PROJECT_ROOT / "config" / "rknn").resolve()
    try:
        toolchain_lock_path.relative_to(toolchain_lock_root)
    except ValueError as error:
        raise core.WorkflowError("RKNN toolchain lock must stay under config/rknn") from error
    if not toolchain_lock_path.is_file():
        raise core.WorkflowError("RKNN platform profile toolchain lock does not exist")
    toolchain_lock = core.load_json(toolchain_lock_path)
    if not isinstance(toolchain_lock, dict) or toolchain_lock.get("family") != "rknn-toolkit2":
        raise core.WorkflowError("RKNN platform profile references an invalid toolchain lock")
    requested_version = parameters["toolchainSpec"].get("version")
    if requested_version and not core._version_satisfies(
        str(toolchain_lock.get("version", "")), requested_version
    ):
        raise core.WorkflowError("RKNN toolchain lock conflicts with the task contract")

    person_detector = None
    person_detector_spec_path = None
    person_detector_spec = None
    if spec.get("model_type") == "classify":
        detector_name = spec.get("calibration", {}).get("person_detector_model")
        if not isinstance(detector_name, str) or not detector_name.strip():
            raise core.WorkflowError(
                "classifier model spec must declare calibration.person_detector_model"
            )
        person_detector_spec_path = _repository_file(
            parameters["raw"].get("personDetectorSpec"),
            f"config/rknn/models/{detector_name}.json",
            PROJECT_ROOT / "config" / "rknn" / "models",
            "parameters.personDetectorSpec",
        )
        person_detector_spec = core.load_json(person_detector_spec_path)
        if (
            not isinstance(person_detector_spec, dict)
            or person_detector_spec.get("name") != detector_name
            or person_detector_spec.get("model_type") != "yolov8_det"
        ):
            raise core.WorkflowError(
                "classifier calibration person-detector spec is invalid"
            )
        detector_source = person_detector_spec.get("source_repository_path")
        if not isinstance(detector_source, str) or not detector_source.strip():
            raise core.WorkflowError(
                "classifier calibration person-detector spec has no source path"
            )
        person_detector = _repository_file(
            parameters["raw"].get("personDetector"),
            detector_source,
            PROJECT_ROOT / "data" / "resource",
            "parameters.personDetector",
        )
    parameters.update(
        {
            "specPath": spec_path,
            "spec": spec,
            "profilePath": profile_path,
            "profile": profile,
            "toolchainLockPath": toolchain_lock_path,
            "toolchainLock": toolchain_lock,
            "calibrationSource": _calibration_source(parameters, run_dir),
            "personDetector": person_detector,
            "personDetectorSpecPath": person_detector_spec_path,
            "personDetectorSpec": person_detector_spec,
        }
    )
    return parameters


def _python_path(parameters: dict[str, Any]) -> str:
    value = parameters["pythonExecutable"]
    resolved = shutil.which(value) if not Path(value).is_absolute() else value
    if not resolved or not Path(resolved).is_file():
        raise core.WorkflowError(f"Python executable is unavailable: {value}")
    return str(resolved)


def _run_stage(
    command: list[str],
    *,
    cwd: Path,
    log_path: Path,
    commands: list[str],
    run_dir: Path,
    environment: dict[str, str] | None,
    detail: str,
    timeout: int = 1800,
) -> None:
    process = common._run_logged(
        command,
        cwd=cwd,
        log_path=log_path,
        commands=commands,
        run_dir=run_dir,
        timeout=timeout,
        environment=environment,
    )
    if process.returncode != 0:
        raise common.ExecutionFailure(detail)


def _onnx_check_command(
    python: str,
    model_path: Path,
    report_path: Path,
    *,
    checker_only: bool,
) -> list[str]:
    command = [
        python,
        str(PROJECT_ROOT / "tools" / "check_onnx_model.py"),
        str(model_path),
    ]
    if checker_only:
        command.append("--checker-only")
    command.extend(["--json", str(report_path)])
    return command


def _prepare_conversion_input(
    parameters: dict[str, Any],
    *,
    python: str,
    attempt_dir: Path,
    logs_dir: Path,
    commands: list[str],
    run_dir: Path,
    environment: dict[str, str] | None,
) -> tuple[Path, list[dict[str, Any]]]:
    source = parameters["sourceModel"]
    spec = parameters["spec"]
    source_hash = core.sha256_file(source)
    expected_source_hash = spec.get("source_sha256")
    if expected_source_hash and source_hash != expected_source_hash:
        raise core.WorkflowError(
            f"RKNN model spec source SHA-256 differs from {common._run_relative(source, run_dir)}"
        )
    adapter = spec.get("conversion", {}).get("output_adapter")
    if adapter == "yolo26_one2one_6head_v1":
        extracted = attempt_dir / f"{parameters['modelName']}-runtime.onnx"
        extracted_report = attempt_dir / "output-extraction.json"
        _run_stage(
            [python, str(PROJECT_ROOT / "tools" / "rknn" / "extract_yolo26_heads.py"),
             "--input", str(source), "--output", str(extracted), "--report", str(extracted_report)],
            cwd=attempt_dir,
            log_path=logs_dir / "S2-output-extraction.log",
            commands=commands,
            run_dir=run_dir,
            environment=environment,
            detail="YOLO26 one-to-one output-head extraction failed",
        )
        expected_input_hash = spec.get("conversion", {}).get("input_sha256")
        if expected_input_hash and core.sha256_file(extracted) != expected_input_hash:
            raise common.ExecutionFailure("YOLO26 extracted RKNN input SHA-256 mismatch")
        return extracted, [common._artifact(extracted_report, run_dir, "output-adapter-provenance")]
    expected_input_hash = spec.get("conversion", {}).get("input_sha256", expected_source_hash)
    if not expected_input_hash or expected_input_hash == source_hash:
        return source, []

    if adapter != "yolo_dfl_6head_v1":
        raise core.WorkflowError(
            "model spec requires a transformed RKNN input but declares no supported transform"
        )
    converted = attempt_dir / f"{parameters['modelName']}-opset.onnx"
    converted_report = attempt_dir / "opset-conversion.json"
    convert_command = [
        python,
        str(PROJECT_ROOT / "tools" / "rknn" / "convert_onnx_opset.py"),
        "--input",
        str(source),
        "--output",
        str(converted),
        "--opset",
        str(spec["conversion"]["maximum_onnx_opset"]),
        "--report",
        str(converted_report),
    ]
    maximum_ir = spec["conversion"].get("maximum_onnx_ir_version")
    if maximum_ir is not None:
        convert_command.extend(["--ir-version", str(maximum_ir)])
    _run_stage(
        convert_command,
        cwd=attempt_dir,
        log_path=logs_dir / "S2-opset-conversion.log",
        commands=commands,
        run_dir=run_dir,
        environment=environment,
        detail="ONNX opset conversion failed",
    )

    extracted = attempt_dir / f"{parameters['modelName']}-runtime.onnx"
    extracted_report = attempt_dir / "output-extraction.json"
    _run_stage(
        [
            python,
            str(PROJECT_ROOT / "tools" / "rknn" / "extract_yolov8_heads.py"),
            "--input",
            str(converted),
            "--output",
            str(extracted),
            "--report",
            str(extracted_report),
        ],
        cwd=attempt_dir,
        log_path=logs_dir / "S2-output-extraction.log",
        commands=commands,
        run_dir=run_dir,
        environment=environment,
        detail="YOLOv8 output-head extraction failed",
    )
    actual_hash = core.sha256_file(extracted)
    if actual_hash != expected_input_hash:
        raise common.ExecutionFailure(
            f"transformed RKNN input SHA-256 mismatch: expected {expected_input_hash}, got {actual_hash}"
        )
    return extracted, [
        common._artifact(converted_report, run_dir, "opset-provenance"),
        common._artifact(extracted_report, run_dir, "output-adapter-provenance"),
    ]


def _prepare_calibration_person_detector(
    parameters: dict[str, Any],
    *,
    python: str,
    attempt_dir: Path,
    verification_dir: Path,
    logs_dir: Path,
    commands: list[str],
    run_dir: Path,
    environment: dict[str, str] | None,
) -> tuple[Path | None, dict[str, Any] | None]:
    source = parameters["personDetector"]
    if source is None:
        return None, None
    spec = parameters["personDetectorSpec"]
    spec_path = parameters["personDetectorSpecPath"]
    expected_source_hash = spec.get("source_sha256")
    source_hash = core.sha256_file(source)
    if expected_source_hash and source_hash != expected_source_hash:
        raise core.WorkflowError(
            "classifier calibration person-detector source SHA-256 differs from its model spec"
        )
    conversion = spec.get("conversion", {})
    maximum_opset = conversion.get("maximum_onnx_opset")
    maximum_ir = conversion.get("maximum_onnx_ir_version")
    if not isinstance(maximum_opset, int) or not isinstance(maximum_ir, int):
        raise core.WorkflowError(
            "classifier calibration person-detector spec must bound ONNX opset and IR versions"
        )

    normalized = attempt_dir / "person-detector-runtime.onnx"
    normalization_report = attempt_dir / "person-detector-normalization.json"
    _run_stage(
        [
            python,
            str(PROJECT_ROOT / "tools" / "rknn" / "convert_onnx_opset.py"),
            "--input",
            str(source),
            "--output",
            str(normalized),
            "--opset",
            str(maximum_opset),
            "--ir-version",
            str(maximum_ir),
            "--report",
            str(normalization_report),
        ],
        cwd=attempt_dir,
        log_path=logs_dir / "S3-person-detector-normalization.log",
        commands=commands,
        run_dir=run_dir,
        environment=environment,
        detail="classifier calibration person-detector normalization failed",
        timeout=300,
    )
    runtime_report = verification_dir / "person-detector-runtime-check.json"
    _run_stage(
        _onnx_check_command(
            python,
            normalized,
            runtime_report,
            checker_only=False,
        ),
        cwd=PROJECT_ROOT,
        log_path=logs_dir / "S3-person-detector-runtime-check.log",
        commands=commands,
        run_dir=run_dir,
        environment=environment,
        detail="normalized classifier calibration person detector failed runtime validation",
        timeout=300,
    )
    return normalized, {
        "source": {
            "path": source.relative_to(PROJECT_ROOT).as_posix(),
            "sha256": source_hash,
        },
        "spec": {
            "path": spec_path.relative_to(PROJECT_ROOT).as_posix(),
            "sha256": core.sha256_file(spec_path),
        },
        "normalizedInput": common._artifact(
            normalized, run_dir, "calibration-person-detector"
        ),
        "normalizationReport": common._artifact(
            normalization_report, run_dir, "calibration-person-detector-provenance"
        ),
        "runtimeReport": common._artifact(
            runtime_report, run_dir, "calibration-person-detector-runtime-check"
        ),
    }


def execute_conversion(
    contract_path: Path, run_dir: Path, contract: dict[str, Any]
) -> tuple[dict[str, Any], dict[str, Any]]:
    parameters = conversion_parameters(contract, run_dir)
    environment_report = common._read_environment_report(contract_path, run_dir, contract)
    selection = common._read_asset_selection(run_dir)
    admitted_toolchain = environment_report["toolchain"]
    current_toolchain, error = core.inspect_toolchain(parameters["toolchainSpec"])
    if not current_toolchain:
        raise core.WorkflowError(f"admitted RKNN toolchain is no longer available: {error}")
    if current_toolchain.get("id") != admitted_toolchain.get("id"):
        raise core.WorkflowError("RKNN toolchain identity changed after admission; rerun doctor")
    if str(current_toolchain.get("package", {}).get("version", "")) != str(
        parameters["toolchainLock"].get("version", "")
    ):
        raise core.WorkflowError("admitted RKNN Toolkit2 version differs from the platform lock")

    attempt, previous_attempts = common._archive_previous_attempt(run_dir)
    work_dir = run_dir / "work" / f"attempt-{attempt}"
    artifacts_dir = run_dir / "artifacts" / f"attempt-{attempt}"
    logs_dir = run_dir / "logs" / f"attempt-{attempt}"
    verification_dir = run_dir / "verification" / f"attempt-{attempt}"
    for directory in (work_dir, artifacts_dir, logs_dir, verification_dir):
        directory.mkdir(parents=True, exist_ok=True, mode=0o700)

    manifest_path = run_dir / "execution-manifest.json"
    commands: list[str] = []
    started = time.monotonic()
    manifest: dict[str, Any] = {
        "schemaVersion": "1.0",
        "status": "RUNNING",
        "attempt": attempt,
        "previousAttempts": previous_attempts,
        "runId": contract["runId"],
        "task": contract["task"],
        "commit": environment_report.get("commit"),
        "tree": environment_report.get("tree"),
        "repository": environment_report["repository"],
        "contractSha256": core.sha256_file(contract_path),
        "routeAssessment": "route-assessment.json",
        "routeAssessmentSha256": environment_report["routeAssessmentSha256"],
        "startedAt": common.utc_now(),
        "source": common._artifact(parameters["sourceModel"], run_dir, "source-onnx"),
        "target": {
            "backend": parameters["targetBackend"],
            "chip": parameters["targetChip"],
            "toolchainChip": parameters["toolchainChip"],
            "quantization": parameters["quantization"],
            "inputLayout": parameters["inputLayout"],
            "inputShapes": parameters["inputShapes"],
            "expectedOutputShapes": parameters["expectedOutputShapes"],
        },
        "modelSpec": {
            "path": parameters["specPath"].relative_to(PROJECT_ROOT).as_posix(),
            "sha256": core.sha256_file(parameters["specPath"]),
        },
        "platformProfile": {
            "path": parameters["profilePath"].relative_to(PROJECT_ROOT).as_posix(),
            "sha256": core.sha256_file(parameters["profilePath"]),
        },
        "toolchainLock": {
            "path": parameters["toolchainLockPath"].relative_to(PROJECT_ROOT).as_posix(),
            "sha256": core.sha256_file(parameters["toolchainLockPath"]),
        },
        "toolchain": current_toolchain,
        "dataFlow": common._data_flow_record(contract),
        "selectedAssets": core.redact_data(selection["selectedAssets"]),
        "assetDifferences": core.redact_data(selection["differences"]),
        "commands": commands,
        "stages": {},
        "artifacts": [],
    }
    core.atomic_write_json(manifest_path, manifest)

    environment = core.toolchain_environment(current_toolchain)
    python = _python_path(parameters)
    try:
        preflight_path = verification_dir / "onnx-check.json"
        _run_stage(
            _onnx_check_command(
                python,
                parameters["sourceModel"],
                preflight_path,
                checker_only=True,
            ),
            cwd=PROJECT_ROOT,
            log_path=logs_dir / "S1-onnx-preflight.log",
            commands=commands,
            run_dir=run_dir,
            environment=environment,
            detail="ONNX preflight failed",
            timeout=300,
        )
        manifest["stages"]["onnxPreflight"] = {
            "status": "PASS",
            "report": common._run_relative(preflight_path, run_dir),
            "reportSha256": core.sha256_file(preflight_path),
        }
        core.atomic_write_json(manifest_path, manifest)

        conversion_input, transform_reports = _prepare_conversion_input(
            parameters,
            python=python,
            attempt_dir=work_dir,
            logs_dir=logs_dir,
            commands=commands,
            run_dir=run_dir,
            environment=environment,
        )
        runtime_check_path = verification_dir / "conversion-input-runtime-check.json"
        _run_stage(
            _onnx_check_command(
                python,
                conversion_input,
                runtime_check_path,
                checker_only=False,
            ),
            cwd=PROJECT_ROOT,
            log_path=logs_dir / "S2-conversion-input-runtime-check.log",
            commands=commands,
            run_dir=run_dir,
            environment=environment,
            detail="normalized RKNN conversion input failed ONNX Runtime smoke validation",
            timeout=300,
        )
        manifest["stages"]["transform"] = {
            "status": "PASS",
            "input": common._artifact(conversion_input, run_dir, "rknn-conversion-input"),
            "reports": transform_reports,
            "runtimeReport": common._run_relative(runtime_check_path, run_dir),
            "runtimeReportSha256": core.sha256_file(runtime_check_path),
        }
        core.atomic_write_json(manifest_path, manifest)

        dataset_path = None
        if parameters["quantization"] == "INT8":
            person_detector, person_detector_evidence = (
                _prepare_calibration_person_detector(
                    parameters,
                    python=python,
                    attempt_dir=work_dir,
                    verification_dir=verification_dir,
                    logs_dir=logs_dir,
                    commands=commands,
                    run_dir=run_dir,
                    environment=environment,
                )
            )
            calibration_dir = work_dir / "calibration"
            calibration_command = [
                python,
                str(PROJECT_ROOT / "tools" / "rknn" / "prepare_validation_data.py"),
                "--spec",
                str(parameters["specPath"]),
                "--video",
                str(parameters["calibrationSource"]),
                "--output-dir",
                str(calibration_dir),
                "--samples",
                str(parameters["spec"]["calibration"]["minimum_samples"]),
            ]
            if person_detector is not None:
                calibration_command.extend(
                    ["--person-detector", str(person_detector)]
                )
            _run_stage(
                calibration_command,
                cwd=work_dir,
                log_path=logs_dir / "S3-calibration.log",
                commands=commands,
                run_dir=run_dir,
                environment=environment,
                detail="representative calibration preparation failed",
            )
            dataset_path = calibration_dir / "dataset.txt"
            calibration_manifest = calibration_dir / "manifest.json"
            manifest["stages"]["calibration"] = {
                "status": "PASS",
                "dataset": common._artifact(dataset_path, run_dir, "calibration-dataset"),
                "manifest": common._artifact(
                    calibration_manifest, run_dir, "calibration-manifest"
                ),
                "personDetector": person_detector_evidence,
            }
        else:
            manifest["stages"]["calibration"] = {
                "status": "SKIP",
                "detail": "FP16 RKNN conversion does not use an INT8 calibration dataset.",
            }
        core.atomic_write_json(manifest_path, manifest)

        precision = "int8" if parameters["quantization"] == "INT8" else "fp16"
        artifact_path = artifacts_dir / f"{parameters['modelName']}_{parameters['targetChip']}_{precision}.rknn"
        build_report_path = verification_dir / "rknn-build.json"
        build_command = [
            python,
            str(PROJECT_ROOT / "tools" / "rknn" / "convert_model.py"),
            "--spec",
            str(parameters["specPath"]),
            "--platform-profile",
            str(parameters["profilePath"]),
            "--model",
            str(conversion_input),
            "--output",
            str(artifact_path),
            "--report",
            str(build_report_path),
        ]
        if parameters["quantization"] == "INT8":
            build_command.extend(["--quantize", "--dataset", str(dataset_path)])
        _run_stage(
            build_command,
            cwd=work_dir,
            log_path=logs_dir / "S4-rknn-build.log",
            commands=commands,
            run_dir=run_dir,
            environment=environment,
            detail="RKNN Toolkit2 build failed",
            timeout=3600,
        )
        artifact = common._artifact(
            artifact_path, run_dir, f"{parameters['targetChip']}-rknn"
        )
        manifest["stages"]["deploy"] = {"status": "PASS", "artifact": artifact}
        manifest["stages"]["modelInfo"] = {
            "status": "PASS",
            "contractMatches": True,
            "report": common._run_relative(build_report_path, run_dir),
            "reportSha256": core.sha256_file(build_report_path),
        }
        package_code = parameters["raw"].get("packageAlgorithmCode")
        if package_code is not None:
            if parameters["spec"].get("model_type") != "yolo26_det":
                raise core.WorkflowError("automatic RKNN package generation supports yolo26_det")
            package_dir = artifacts_dir / (
                f"prod_{parameters['targetChip'].upper()}_{package_code}_YOLO26_V1.0.0"
            )
            package_report = verification_dir / "package-report.json"
            _run_stage(
                [python, str(PROJECT_ROOT / "tools" / "rknn" / "package_yolo26.py"),
                 "--source-onnx", str(parameters["sourceModel"]),
                 "--model", str(artifact_path), "--output-dir", str(package_dir),
                 "--algorithm-code", str(package_code), "--chip", parameters["targetChip"],
                 "--report", str(package_report)],
                cwd=work_dir,
                log_path=logs_dir / "S4-package.log",
                commands=commands,
                run_dir=run_dir,
                environment=environment,
                detail="YOLO26 RKNN package generation failed",
            )
            manifest["stages"]["package"] = {
                "status": "PASS",
                "model": common._artifact(package_dir / "model.rknn", run_dir, "package-model"),
                "config": common._artifact(package_dir / "config.json", run_dir, "package-config"),
                "report": common._artifact(package_report, run_dir, "package-report"),
            }
        manifest["stages"]["tensorCompare"] = {
            "status": "UNVERIFIED",
            "detail": "Numerical parity is deferred to the target-bound RKNN runtime validation.",
        }
        manifest["artifacts"] = [artifact]
        manifest["status"] = "COMPLETE"
        manifest["completedAt"] = common.utc_now()
        manifest["durationSeconds"] = round(time.monotonic() - started, 3)
        core.atomic_write_json(manifest_path, manifest)
        return manifest, parameters
    except (core.WorkflowError, common.ExecutionFailure) as error:
        common._failed_manifest(manifest_path, manifest, str(error))
        raise


def _manifest_file(run_dir: Path, entry: Any, role: str) -> Path:
    if not isinstance(entry, dict) or not isinstance(entry.get("path"), str):
        raise core.WorkflowError(f"execution manifest has no {role} path")
    path = core.resolve_run_input(run_dir, entry["path"])
    if core.sha256_file(path) != entry.get("sha256"):
        raise core.WorkflowError(f"execution manifest {role} SHA-256 changed")
    return path


def verify_conversion(
    contract_path: Path, run_dir: Path, contract: dict[str, Any]
) -> dict[str, Any]:
    parameters = conversion_parameters(contract, run_dir)
    environment = common._read_environment_report(contract_path, run_dir, contract)
    selection = common._read_asset_selection(run_dir)
    manifest_path = run_dir / "execution-manifest.json"
    manifest = core.load_json(manifest_path)
    if not isinstance(manifest, dict) or manifest.get("status") != "COMPLETE":
        raise core.WorkflowError("execution-manifest.json is absent or not COMPLETE")
    if manifest.get("runId") != contract["runId"]:
        raise core.WorkflowError("execution manifest belongs to another run")
    if manifest.get("contractSha256") != core.sha256_file(contract_path):
        raise core.WorkflowError("execution manifest does not match the current contract")

    stages: list[dict[str, str]] = []
    try:
        source = _manifest_file(run_dir, manifest.get("source"), "source")
        preflight = manifest.get("stages", {}).get("onnxPreflight", {})
        preflight_report = _manifest_file(
            run_dir,
            {"path": preflight.get("report"), "sha256": preflight.get("reportSha256")},
            "ONNX preflight report",
        )
        preflight_data = core.load_json(preflight_report)
        source_ok = (
            isinstance(preflight_data, dict)
            and preflight.get("status") == "PASS"
            and preflight_data.get("status") == "PASS"
            and preflight_data.get("validationMode") == "checker-only"
            and preflight_data.get("model", {}).get("sha256") == core.sha256_file(source)
        )
    except core.WorkflowError:
        source_ok = False
    stages.append(
        {
            "id": "S1",
            "status": "PASS" if source_ok else "FAIL",
            "detail": "ONNX source checker and source identity match." if source_ok else "ONNX source-check evidence is missing or changed.",
        }
    )

    transform = manifest.get("stages", {}).get("transform", {})
    try:
        conversion_input = _manifest_file(run_dir, transform.get("input"), "conversion input")
        runtime_report = _manifest_file(
            run_dir,
            {
                "path": transform.get("runtimeReport"),
                "sha256": transform.get("runtimeReportSha256"),
            },
            "conversion-input runtime report",
        )
        runtime_data = core.load_json(runtime_report)
        expected_hash = parameters["spec"].get("conversion", {}).get(
            "input_sha256", parameters["spec"].get("source_sha256")
        )
        input_hash = core.sha256_file(conversion_input)
        transform_ok = (
            transform.get("status") == "PASS"
            and (not expected_hash or input_hash == expected_hash)
            and isinstance(runtime_data, dict)
            and runtime_data.get("status") == "PASS"
            and runtime_data.get("validationMode") == "runtime-smoke"
            and runtime_data.get("model", {}).get("sha256") == input_hash
        )
    except core.WorkflowError:
        transform_ok = False
    stages.append(
        {
            "id": "S2",
            "status": "PASS" if transform_ok else "FAIL",
            "detail": "The target-independent model contract resolves to a runtime-checked RKNN conversion input." if transform_ok else "RKNN conversion-input identity or runtime evidence is missing or changed.",
        }
    )

    calibration = manifest.get("stages", {}).get("calibration", {})
    if parameters["quantization"] == "INT8":
        try:
            _manifest_file(run_dir, calibration.get("dataset"), "calibration dataset")
            calibration_manifest_path = _manifest_file(
                run_dir, calibration.get("manifest"), "calibration manifest"
            )
            calibration_data = core.load_json(calibration_manifest_path)
            if not isinstance(calibration_data, dict):
                raise core.WorkflowError("calibration manifest must be an object")
            person_detector_ok = calibration_data.get("person_detector") is None
            if parameters["personDetector"] is not None:
                detector = calibration.get("personDetector")
                if not isinstance(detector, dict):
                    raise core.WorkflowError(
                        "calibration person-detector evidence is missing"
                    )
                normalized_detector = _manifest_file(
                    run_dir,
                    detector.get("normalizedInput"),
                    "calibration person-detector input",
                )
                normalization_report_path = _manifest_file(
                    run_dir,
                    detector.get("normalizationReport"),
                    "calibration person-detector normalization report",
                )
                detector_runtime_report_path = _manifest_file(
                    run_dir,
                    detector.get("runtimeReport"),
                    "calibration person-detector runtime report",
                )
                normalization_data = core.load_json(normalization_report_path)
                detector_runtime_data = core.load_json(detector_runtime_report_path)
                if not isinstance(normalization_data, dict) or not isinstance(
                    detector_runtime_data, dict
                ):
                    raise core.WorkflowError(
                        "calibration person-detector reports must be objects"
                    )
                normalized_hash = core.sha256_file(normalized_detector)
                expected_output_shape = parameters["personDetectorSpec"]["outputs"][0][
                    "shape"
                ]
                output_shapes = [
                    item.get("runtimeShape")
                    for item in detector_runtime_data.get("outputs", [])
                    if isinstance(item, dict)
                ]
                source_record = detector.get("source", {})
                spec_record = detector.get("spec", {})
                person_detector_ok = (
                    source_record.get("path")
                    == parameters["personDetector"].relative_to(PROJECT_ROOT).as_posix()
                    and source_record.get("sha256")
                    == core.sha256_file(parameters["personDetector"])
                    and spec_record.get("path")
                    == parameters["personDetectorSpecPath"]
                    .relative_to(PROJECT_ROOT)
                    .as_posix()
                    and spec_record.get("sha256")
                    == core.sha256_file(parameters["personDetectorSpecPath"])
                    and normalization_data.get("source", {}).get("sha256")
                    == source_record.get("sha256")
                    and normalization_data.get("converted", {}).get("sha256")
                    == normalized_hash
                    and detector_runtime_data.get("status") == "PASS"
                    and detector_runtime_data.get("validationMode") == "runtime-smoke"
                    and detector_runtime_data.get("model", {}).get("sha256")
                    == normalized_hash
                    and expected_output_shape in output_shapes
                    and calibration_data.get("person_detector", {}).get("sha256")
                    == normalized_hash
                )
            calibration_ok = (
                calibration.get("status") == "PASS"
                and person_detector_ok
                and len(calibration_data.get("samples", []))
                >= int(parameters["spec"]["calibration"]["minimum_samples"])
            )
        except (core.WorkflowError, KeyError, TypeError):
            calibration_ok = False
        stages.append(
            {
                "id": "S3",
                "status": "PASS" if calibration_ok else "FAIL",
                "detail": "Representative INT8 calibration inputs and identities are recorded." if calibration_ok else "INT8 calibration evidence is incomplete or changed.",
            }
        )
    else:
        stages.append(
            {"id": "S3", "status": "SKIP", "detail": "FP16 conversion does not require calibration."}
        )

    artifact_entry = manifest.get("artifacts", [None])[0]
    model_info = manifest.get("stages", {}).get("modelInfo", {})
    try:
        artifact_path = _manifest_file(run_dir, artifact_entry, "RKNN artifact")
        build_report_path = _manifest_file(
            run_dir,
            {"path": model_info.get("report"), "sha256": model_info.get("reportSha256")},
            "RKNN build report",
        )
        build_report = core.load_json(build_report_path)
        artifact_ok = (
            isinstance(build_report, dict)
            and model_info.get("status") == "PASS"
            and model_info.get("contractMatches") is True
            and build_report.get("build", {}).get("target_platform") == parameters["targetChip"]
            and build_report.get("artifact", {}).get("sha256") == core.sha256_file(artifact_path)
            and build_report.get("spec", {}).get("sha256") == core.sha256_file(parameters["specPath"])
            and build_report.get("platform_profile", {}).get("sha256")
            == core.sha256_file(parameters["profilePath"])
            and manifest.get("toolchainLock", {}).get("sha256")
            == core.sha256_file(parameters["toolchainLockPath"])
        )
    except (core.WorkflowError, IndexError, TypeError):
        artifact_path = None
        artifact_ok = False
    stages.append(
        {
            "id": "S4",
            "status": "PASS" if artifact_ok else "FAIL",
            "detail": "RKNN artifact, target profile, model contract, toolchain report, and hashes agree." if artifact_ok else "RKNN artifact contract evidence is incomplete or changed.",
        }
    )
    package_code = parameters["raw"].get("packageAlgorithmCode")
    if package_code is not None:
        package_stage = manifest.get("stages", {}).get("package", {})
        try:
            package_model = _manifest_file(run_dir, package_stage.get("model"), "package model")
            package_config = _manifest_file(run_dir, package_stage.get("config"), "package config")
            package_report = _manifest_file(run_dir, package_stage.get("report"), "package report")
            config = core.load_json(package_config)
            report = core.load_json(package_report)
            package_ok = (
                package_stage.get("status") == "PASS"
                and isinstance(config, dict)
                and isinstance(report, dict)
                and package_model.read_bytes() == artifact_path.read_bytes()
                and config.get("algorithm_code") == str(package_code)
                and config.get("chip_type") == parameters["targetChip"].upper()
                and config.get("model_type") == parameters["spec"].get("model_type")
                and config.get("models", [{}])[0].get("file_name") == "model.rknn"
                and config.get("models", [{}])[0].get("file_md5")
                == hashlib.md5(package_model.read_bytes()).hexdigest()
                and report.get("model_sha256") == core.sha256_file(package_model)
                and report.get("config_sha256") == core.sha256_file(package_config)
            )
        except (core.WorkflowError, IndexError, TypeError, AttributeError):
            package_ok = False
        stages.append({"id": "S4P", "status": "PASS" if package_ok else "FAIL",
                       "detail": "RKNN model package and config identities agree." if package_ok
                       else "RKNN package evidence is incomplete or changed."})
    stages.append(
        {
            "id": "S5",
            "status": "UNVERIFIED",
            "detail": "Target runtime, numerical parity, video pipeline, and business acceptance remain separate device gates.",
        }
    )

    required_ids = {"S1", "S2", "S4"}
    if package_code is not None:
        required_ids.add("S4P")
    if parameters["quantization"] == "INT8":
        required_ids.add("S3")
    required = [item for item in stages if item["id"] in required_ids]
    development_verdict = "COMPLETE" if all(item["status"] == "PASS" for item in required) else "FAILED"
    deliverables = []
    if artifact_path is not None:
        deliverables.append(
            {
                "path": artifact_entry["path"],
                "sha256": artifact_entry["sha256"],
                "sizeBytes": artifact_path.stat().st_size,
                "role": artifact_entry.get("role", "rknn"),
            }
        )
    evidence = {
        "schemaVersion": "1.0",
        "runId": contract["runId"],
        "task": contract["task"],
        "userObjective": core.redact_text(contract["userObjective"]),
        "commit": environment.get("commit"),
        "timestamp": common.utc_now(),
        "environmentReport": "environment-report.json",
        "routeAssessment": "route-assessment.json",
        "routeAssessmentSha256": environment["routeAssessmentSha256"],
        "contractSha256": core.sha256_file(contract_path),
        "authorityGrants": sorted(core._authority_grants(contract)),
        "selectedAssets": core.redact_data(selection["selectedAssets"]),
        "assetDifferences": core.redact_data(selection["differences"]),
        "attempts": [
            *manifest.get("previousAttempts", []),
            common._attempt_summary(manifest),
        ],
        "dataFlow": core.redact_data(manifest.get("dataFlow", {})),
        "stages": stages,
        "deliverables": deliverables,
        "developmentVerdict": development_verdict,
        "promotionVerdict": "NOT_REQUESTED",
        "deviceVerdict": "NOT_RUN",
        "pendingOnDevice": [
            "RKNN runtime import and NPU execution",
            "ONNX-to-RKNN numerical comparison",
            "one-stream video, OSD, rule, and alarm loop",
            "stability and resource measurements",
        ],
        "commands": [core.redact_text(str(item)) for item in manifest.get("commands", [])],
    }
    core.atomic_write_json(run_dir / "evidence.json", evidence)
    lines = [
        "# RKNN model conversion evidence",
        "",
        f"- Development conversion: **{development_verdict}**",
        "- Device result: **NOT_RUN**",
        "",
        "## Layered verification",
        "",
        *[f"- {item['id']} {item['status']}: {item['detail']}" for item in stages],
        "",
        "## Deliverables",
        "",
        *[
            f"- `{item['path']}` — SHA-256 `{item['sha256']}` ({item['sizeBytes']} bytes)"
            for item in deliverables
        ],
        "",
        "Conversion completion does not imply RV1126B device or production acceptance.",
        "",
    ]
    common._write_private_text(run_dir / "evidence.md", "\n".join(lines))
    return evidence


def convert_main(arguments: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="convert_model.sh")
    parser.add_argument("--contract", required=True)
    options = parser.parse_args(arguments)
    contract_path, run_dir, contract = core.resolve_contract_context(options.contract)
    try:
        manifest, _ = execute_conversion(contract_path, run_dir, contract)
        print(f"Conversion artifact: {manifest['artifacts'][0]['path']}")
        print("Execution manifest: execution-manifest.json")
        return 0
    except common.ExecutionFailure as error:
        print(f"conversion failed: {error}", file=sys.stderr)
        return 1


def verify_main(arguments: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="verify.sh")
    parser.add_argument("--contract", required=True)
    options = parser.parse_args(arguments)
    contract_path, run_dir, contract = core.resolve_contract_context(options.contract)
    evidence = verify_conversion(contract_path, run_dir, contract)
    print(f"Development conversion: {evidence['developmentVerdict']}")
    print("Device validation: NOT_RUN")
    return 0 if evidence["developmentVerdict"] == "COMPLETE" else 1


def main(arguments: list[str] | None = None) -> int:
    args = list(sys.argv[1:] if arguments is None else arguments)
    if not args:
        print("usage: agent_conversion_workflow.py convert|verify ...", file=sys.stderr)
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
    raise SystemExit(main())
