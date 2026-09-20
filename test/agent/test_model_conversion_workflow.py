import io
import json
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import agent_workflow as core  # noqa: E402
import check_onnx_model  # noqa: E402
import model_conversion_workflow as conversion  # noqa: E402


def make_contract(run_id: str = "conversion-test") -> dict:
    return {
        "schemaVersion": "1.0",
        "runId": run_id,
        "task": "model-conversion",
        "userObjective": "Convert the supplied ONNX model for a BM1688 development task.",
        "expectedDeliverables": ["F16 bmodel", "evidence"],
        "allowedChanges": ["current run directory"],
        "requiredCapabilities": ["python3"],
        "acceptance": {
            "requireTensorCompare": True,
            "promoteExample": False,
        },
        "authority": {
            "workspace": "isolated test checkout",
            "environmentChanges": False,
            "externalSystems": False,
        },
        "parameters": {
            "sourceModel": "inputs/candidate.onnx",
            "modelName": "candidate_320",
            "modelFamily": "fixture detector",
            "sourceUrl": "https://example.invalid/candidate.onnx",
            "recordedBy": "fixture-maintainer",
            "targetBackend": "Sophon BMRT",
            "targetChip": "bm1688",
            "toolchainChip": "bm1688",
            "quantization": "F16",
            "inputLayout": "NCHW",
            "inputShapes": [[1, 3, 320, 320]],
            "expectedOutputShapes": [[1, 6, 2100]],
            "pixelFormat": "rgb",
            "toolchain": {
                "kind": "python-package",
                "pythonExecutable": sys.executable,
                "package": "tpu_mlir",
                "version": "1.28.1",
            },
            "preflight": {
                "pythonExecutable": sys.executable,
                "pythonPackages": {
                    "numpy": None,
                    "onnx": None,
                    "onnxruntime": None,
                },
            },
            "tensorTolerance": "0.99,0.90",
            "outputKind": "bmodel",
        },
    }


def make_toolchain_identity() -> dict:
    return {
        "kind": "python-package",
        "id": "sha256:" + "a" * 64,
        "pythonExecutable": "/isolated/venv/bin/python",
        "pythonVersion": "3.10.12",
        "sysPrefix": "/isolated/venv",
        "basePrefix": "/usr",
        "package": {
            "name": "tpu_mlir",
            "version": "1.28.1",
            "recordSha256": "b" * 64,
        },
        "tools": {
            "modelTransform": {
                "path": "/isolated/venv/bin/model_transform.py",
                "sha256": "c" * 64,
            },
            "modelDeploy": {
                "path": "/isolated/venv/bin/model_deploy.py",
                "sha256": "d" * 64,
            },
            "modelTool": {
                "path": "/isolated/venv/bin/model_tool",
                "sha256": "e" * 64,
            },
        },
    }


def prepare_run(root: Path, contract: dict) -> tuple[Path, Path]:
    run_dir = root / "output" / "agent-runs" / contract["runId"]
    (run_dir / "inputs").mkdir(parents=True)
    (run_dir / "inputs" / "candidate.onnx").write_bytes(b"synthetic-onnx-fixture")
    contract_path = run_dir / "task-contract.json"
    contract_path.write_text(json.dumps(contract), encoding="utf-8")
    route = {
        "schemaVersion": "1.0",
        "mode": "assessment",
        "task": contract["task"],
        "runId": contract["runId"],
        "contractSha256": core.sha256_file(contract_path),
        "needsInput": [],
        "routeVerdict": "READY",
    }
    (run_dir / "route-assessment.json").write_text(json.dumps(route), encoding="utf-8")
    return run_dir, contract_path


class ModelConversionWorkflowTest(unittest.TestCase):
    def test_example_catalog_starts_truthfully_empty_and_json_assets_parse(self):
        example_dir = ROOT / "test" / "agent" / "examples" / "model-conversion"
        index = json.loads((example_dir / "index.json").read_text(encoding="utf-8"))
        schema = json.loads((example_dir / "schema.json").read_text(encoding="utf-8"))
        self.assertEqual(index["examples"], [])
        self.assertEqual(index["lifecycle"], "beta")
        self.assertIn("recordings", schema["required"])
        self.assertEqual(
            schema["properties"]["status"]["enum"],
            ["candidate", "conversion-verified"],
        )
        self.assertEqual(
            schema["properties"]["lifecycle"]["properties"]["status"]["enum"],
            ["active", "revoked"],
        )
        self.assertIn("deviceValidation", schema["properties"])

    def test_checker_only_cli_does_not_request_runtime_execution(self):
        with tempfile.TemporaryDirectory() as directory:
            model_path = Path(directory) / "source.onnx"
            report_path = Path(directory) / "report.json"
            model_path.write_bytes(b"fixture")
            result = {
                "schemaVersion": "1.0",
                "status": "PASS",
                "validationMode": "checker-only",
                "model": {"path": model_path.name, "sha256": "fixture"},
                "inputs": [],
                "outputs": [],
            }
            with mock.patch.object(
                check_onnx_model, "inspect_model", return_value=result
            ) as inspect:
                self.assertEqual(
                    check_onnx_model.main(
                        [str(model_path), "--checker-only", "--json", str(report_path)]
                    ),
                    0,
                )
            inspect.assert_called_once_with(model_path.resolve(), {}, run_runtime=False)
            self.assertEqual(
                json.loads(report_path.read_text())["validationMode"], "checker-only"
            )

    def test_candidate_shapes_and_chip_come_from_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            run_dir, _ = prepare_run(Path(directory), make_contract())
            parameters = conversion.conversion_parameters(make_contract(), run_dir)
            transform = conversion.build_transform_arguments(
                parameters,
                source_model="/workspace/run/inputs/candidate.onnx",
                mlir="/workspace/run/work/candidate.mlir",
            )
            deploy = conversion.build_deploy_arguments(
                parameters,
                mlir="/workspace/run/work/candidate.mlir",
                model="/workspace/run/artifacts/candidate.bmodel",
            )
            self.assertIn("[[1,3,320,320]]", transform)
            self.assertNotIn("640", " ".join(transform))
            self.assertEqual(deploy[deploy.index("--chip") + 1], "bm1688")
            self.assertEqual(deploy[deploy.index("--quantize") + 1], "F16")

    def test_tensor_comparison_uses_tool_default_when_contract_has_no_tolerance(self):
        with tempfile.TemporaryDirectory() as directory:
            contract = make_contract()
            contract["parameters"].pop("tensorTolerance")
            run_dir, _ = prepare_run(Path(directory), contract)
            parameters = conversion.conversion_parameters(contract, run_dir)
            deploy = conversion.build_deploy_arguments(
                parameters,
                mlir="/workspace/run/work/candidate.mlir",
                model="/workspace/run/artifacts/candidate.bmodel",
                test_input="/workspace/run/inputs/sample.npz",
                test_reference="/workspace/run/work/reference.npz",
            )
            self.assertIn("--test_input", deploy)
            self.assertIn("--test_reference", deploy)
            self.assertNotIn("--tolerance", deploy)
            self.assertIsNone(parameters["tensorTolerance"])

    def test_precision_tables_are_resolved_and_forwarded_in_both_runtimes(self):
        with tempfile.TemporaryDirectory() as directory:
            contract = make_contract()
            contract["parameters"].update({
                "quantization": "INT8",
                "quantizeTable": "inputs/precision.qtable",
                "calibrationTable": "inputs/calibration.table",
            })
            run_dir, _ = prepare_run(Path(directory), contract)
            (run_dir / "inputs/precision.qtable").write_text("# sensitive layers\n/index F32\n/head F16\n")
            (run_dir / "inputs/calibration.table").write_text("# op threshold min max\n/features 1.5 -0.5 1.5\n")
            parameters = conversion.conversion_parameters(contract, run_dir)
            for kind in ("python-package", "container-image"):
                with self.subTest(kind=kind):
                    identity = {"kind": kind}
                    arguments = conversion.build_deploy_arguments(
                        parameters, mlir="candidate.mlir", model="candidate.bmodel",
                        quantize_table=conversion._runtime_path(parameters["quantizeTable"], run_dir, identity),
                        calibration_table=conversion._runtime_path(parameters["calibrationTable"], run_dir, identity),
                    )
                    prefix = "/workspace/run" if kind == "container-image" else str(run_dir.resolve())
                    self.assertEqual(arguments[arguments.index("--quantize_table") + 1], prefix + "/inputs/precision.qtable")
                    self.assertEqual(arguments[arguments.index("--calibration_table") + 1], prefix + "/inputs/calibration.table")
            entries = conversion._conversion_inputs(parameters, run_dir)
            self.assertEqual([entry["role"] for entry in entries], ["quantization-table", "calibration-table"])
            self.assertEqual(entries[0]["sha256"], core.sha256_file(parameters["quantizeTable"]))

    def test_int8_requires_a_nonempty_valid_calibration_table(self):
        with tempfile.TemporaryDirectory() as directory:
            contract = make_contract()
            contract["parameters"]["quantization"] = "INT8"
            run_dir, _ = prepare_run(Path(directory), contract)
            with self.assertRaisesRegex(core.WorkflowError, "requires parameters.calibrationTable"):
                conversion.conversion_parameters(contract, run_dir)
            contract["parameters"]["calibrationTable"] = "inputs/calibration.table"
            for text in ("", "# only comment\n", "layer 1\n", "layer NaN 0 1\n", "layer 0 0 1\n", "layer 1 2 0\n"):
                with self.subTest(text=text):
                    (run_dir / "inputs/calibration.table").write_text(text)
                    with self.assertRaises(core.WorkflowError):
                        conversion.conversion_parameters(contract, run_dir)

    def test_precision_table_paths_cannot_escape_run_including_symlinks(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract = make_contract()
            run_dir, _ = prepare_run(root, contract)
            outside = root / "outside.table"
            outside.write_text("layer F32\n")
            (run_dir / "inputs/link.table").symlink_to(outside)
            for value in (str(outside), "../../../outside.table", "inputs/link.table", "inputs/absent.table"):
                with self.subTest(value=value):
                    contract["parameters"]["quantizeTable"] = value
                    with self.assertRaises(core.WorkflowError):
                        conversion.conversion_parameters(contract, run_dir)

    def test_precision_table_changes_invalidate_verification_and_seal(self):
        for key, original, changed in (
            ("quantizeTable", "index F32\n", "index F16\n"),
            ("calibrationTable", "features 1.5 -0.5 1.5\n", "features 2.0 -0.5 2.0\n"),
        ):
            with self.subTest(key=key), tempfile.TemporaryDirectory() as directory:
                run_dir, contract_path, contract = self._make_verifiable_run(Path(directory), "PASS")
                contract["parameters"][key] = "inputs/precision.table"
                table = run_dir / "inputs/precision.table"
                table.write_text(original)
                parameters = conversion.conversion_parameters(contract, run_dir)
                contract_path.write_text(json.dumps(contract))
                route_path = run_dir / "route-assessment.json"
                route = json.loads(route_path.read_text())
                route["contractSha256"] = core.sha256_file(contract_path)
                route_path.write_text(json.dumps(route))
                for filename in ("environment-report.json", "execution-manifest.json"):
                    path = run_dir / filename
                    record = json.loads(path.read_text())
                    record["contractSha256"] = core.sha256_file(contract_path)
                    record["routeAssessmentSha256"] = core.sha256_file(route_path)
                    if filename == "execution-manifest.json":
                        record["conversionInputs"] = conversion._conversion_inputs(parameters, run_dir)
                    path.write_text(json.dumps(record))
                before = conversion.verify_conversion(contract_path, run_dir, contract)
                self.assertEqual(before["developmentVerdict"], "COMPLETE")
                table.write_text(changed)
                after = conversion.verify_conversion(contract_path, run_dir, contract)
                self.assertEqual(after["developmentVerdict"], "FAILED")
                stage = next(stage for stage in after["stages"] if stage["id"] == "S2")
                self.assertEqual(stage["status"], "FAIL")
                self.assertIn("table inputs changed", stage["detail"])
                seal, reason = conversion.issue_verification_seal(run_dir, before)
                self.assertIsNone(seal)
                self.assertIn("table inputs changed", reason)

    def test_python_toolchain_uses_selected_interpreter_not_entry_shebang(self):
        identity = make_toolchain_identity()
        command = conversion._tool_command(
            identity,
            "modelTransform",
            ["--help"],
            run_dir=ROOT / "output" / "agent-runs" / "fixture",
        )
        self.assertEqual(command[0], identity["pythonExecutable"])
        self.assertEqual(command[1], identity["tools"]["modelTransform"]["path"])
        self.assertEqual(command[2:], ["--help"])
        environment = core.toolchain_environment(identity)
        self.assertIsNotNone(environment)
        self.assertEqual(
            environment["PATH"].split(os.pathsep)[0],
            str(Path(identity["pythonExecutable"]).parent),
        )
        self.assertEqual(
            environment["VIRTUAL_ENV"],
            str(Path(identity["pythonExecutable"]).parent.parent),
        )

    def test_direct_tool_entry_does_not_require_python_script_layout(self):
        identity = make_toolchain_identity()
        identity["tools"]["modelTransform"]["invocation"] = "direct"
        command = conversion._tool_command(
            identity,
            "modelTransform",
            ["--help"],
            run_dir=ROOT / "output" / "agent-runs" / "fixture",
        )
        self.assertEqual(command, [identity["tools"]["modelTransform"]["path"], "--help"])

    def test_system_python_identity_does_not_inherit_unrelated_virtualenv(self):
        identity = make_toolchain_identity()
        identity["sysPrefix"] = "/usr"
        identity["basePrefix"] = "/usr"
        previous = os.environ.get("VIRTUAL_ENV")
        os.environ["VIRTUAL_ENV"] = "/unrelated/venv"
        try:
            environment = core.toolchain_environment(identity)
        finally:
            if previous is None:
                os.environ.pop("VIRTUAL_ENV", None)
            else:
                os.environ["VIRTUAL_ENV"] = previous
        self.assertNotIn("VIRTUAL_ENV", environment)

    def test_contract_source_cannot_escape_the_run(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract = make_contract()
            run_dir, _ = prepare_run(root, contract)
            outside = root / "outside.onnx"
            outside.write_bytes(b"synthetic")
            contract["parameters"]["sourceModel"] = "../../../outside.onnx"
            with self.assertRaisesRegex(core.WorkflowError, "escapes"):
                conversion.conversion_parameters(contract, run_dir)

    def test_dynamic_shape_resolution_is_bounded_and_explicit(self):
        self.assertEqual(check_onnx_model.runtime_shape([1, 3, "height", None], None), [1, 3, 1, 1])
        self.assertEqual(
            check_onnx_model.runtime_shape([1, 3, "height", "width"], [1, 3, 320, 320]),
            [1, 3, 320, 320],
        )
        with self.assertRaisesRegex(check_onnx_model.OnnxCheckError, "rank"):
            check_onnx_model.runtime_shape([1, 3, 320, 320], [1, 3, 320])

    def test_environment_report_is_bound_to_contract_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract = make_contract()
            run_dir, contract_path = prepare_run(root, contract)
            report = {
                "runId": contract["runId"],
                "task": contract["task"],
                "contractSha256": core.sha256_file(contract_path),
                "routeAssessmentSha256": core.sha256_file(
                    run_dir / "route-assessment.json"
                ),
                "environmentVerdict": "READY",
                "repository": core._git_snapshot(),
                "toolchain": make_toolchain_identity(),
            }
            (run_dir / "environment-report.json").write_text(json.dumps(report), encoding="utf-8")
            self.assertEqual(
                conversion._read_environment_report(contract_path, run_dir, contract)["environmentVerdict"],
                "READY",
            )
            contract["userObjective"] = "changed after admission"
            contract_path.write_text(json.dumps(contract), encoding="utf-8")
            with self.assertRaisesRegex(core.WorkflowError, "changed after"):
                conversion._read_environment_report(contract_path, run_dir, contract)

    def test_conversion_rejects_missing_or_changed_route_assessment(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract = make_contract()
            run_dir, contract_path = prepare_run(root, contract)
            route_path = run_dir / "route-assessment.json"
            original_route = route_path.read_text(encoding="utf-8")
            report = {
                "runId": contract["runId"],
                "task": contract["task"],
                "contractSha256": core.sha256_file(contract_path),
                "routeAssessmentSha256": core.sha256_file(route_path),
                "environmentVerdict": "READY",
                "repository": core._git_snapshot(),
                "toolchain": make_toolchain_identity(),
            }
            (run_dir / "environment-report.json").write_text(
                json.dumps(report), encoding="utf-8"
            )
            route_path.unlink()
            with self.assertRaisesRegex(core.WorkflowError, "file does not exist"):
                conversion._read_environment_report(contract_path, run_dir, contract)

            route_path.write_text(original_route, encoding="utf-8")
            route = json.loads(original_route)
            route["recommendedRoute"] = "changed-after-doctor"
            route_path.write_text(json.dumps(route), encoding="utf-8")
            with self.assertRaisesRegex(core.WorkflowError, "changed after environment admission"):
                conversion._read_environment_report(contract_path, run_dir, contract)

    def test_previous_execution_manifest_is_archived_before_rerun(self):
        with tempfile.TemporaryDirectory() as directory:
            run_dir = Path(directory)
            previous = {
                "schemaVersion": "1.0",
                "attempt": 1,
                "status": "FAILED",
                "startedAt": "2026-01-01T00:00:00Z",
                "completedAt": "2026-01-01T00:01:00Z",
                "failure": "tensor comparison failed",
                "stages": {"tensorCompare": {"status": "FAIL"}},
                "artifacts": [],
            }
            (run_dir / "execution-manifest.json").write_text(
                json.dumps(previous), encoding="utf-8"
            )
            attempt, history = conversion._archive_previous_attempt(run_dir)
            self.assertEqual(attempt, 2)
            self.assertEqual(history[0]["stageStatuses"]["tensorCompare"], "FAIL")
            self.assertEqual(history[0]["archive"], "attempts/attempt-1.json")
            archived = json.loads(
                (run_dir / "attempts" / "attempt-1.json").read_text(encoding="utf-8")
            )
            self.assertEqual(archived["status"], "FAILED")
            self.assertEqual(
                history[0]["manifestSha256"],
                core.sha256_file(run_dir / "attempts" / "attempt-1.json"),
            )

    def test_data_flow_record_is_coarse_and_redacted(self):
        contract = make_contract()
        contract["parameters"]["requiresModelTransfer"] = True
        contract["parameters"]["dataFlow"] = {
            "status": "COMPLETED",
            "sourceZone": "https://user:secret@example.invalid/source",
            "executionZone": "isolated Linux development environment",
            "evidenceReference": "transfer-log-sha256:fixture",
            "password": "must-not-be-copied",
        }
        record = conversion._data_flow_record(contract)
        self.assertEqual(record["mode"], "REMOTE_TRANSFER")
        self.assertEqual(record["status"], "COMPLETED")
        self.assertNotIn("secret", json.dumps(record))
        self.assertNotIn("must-not-be-copied", json.dumps(record))
        self.assertFalse(record["credentialMaterialStored"])

    def _make_verifiable_run(self, root: Path, tensor_status: str) -> tuple[Path, Path, dict]:
        contract = make_contract()
        run_dir, contract_path = prepare_run(root, contract)
        source_path = run_dir / "inputs" / "candidate.onnx"
        (run_dir / "verification").mkdir()
        preflight_path = run_dir / "verification" / "onnx-check.json"
        preflight = {
            "schemaVersion": "1.0",
            "status": "PASS",
            "model": {"sha256": core.sha256_file(source_path)},
        }
        preflight_path.write_text(json.dumps(preflight), encoding="utf-8")
        (run_dir / "artifacts").mkdir()
        artifact_path = run_dir / "artifacts" / "candidate_320_bm1688_f16.bmodel"
        artifact_path.write_bytes(b"synthetic-bmodel-fixture")
        repository = core._git_snapshot()
        repository.update({"trackedChanges": 0, "untrackedFiles": 0})
        environment = {
            "schemaVersion": "1.0",
            "runId": contract["runId"],
            "task": contract["task"],
            "commit": repository["commit"],
            "tree": repository["tree"],
            "contractSha256": core.sha256_file(contract_path),
            "routeAssessmentSha256": core.sha256_file(
                run_dir / "route-assessment.json"
            ),
            "environmentVerdict": "READY",
            "repository": repository,
            "toolchain": make_toolchain_identity(),
            "checks": [
                {
                    "id": "compatibility-matrix",
                    "status": "PASS",
                    "detail": "synthetic repository-backed mapping",
                }
            ],
        }
        (run_dir / "environment-report.json").write_text(json.dumps(environment), encoding="utf-8")
        selection = {
            "selectedAssets": ["docs/tutorials/05-model-porting/model-porting.md"],
            "differences": ["fixture uses 320x320 input"],
        }
        (run_dir / "asset-selection.json").write_text(json.dumps(selection), encoding="utf-8")
        manifest = {
            "schemaVersion": "1.0",
            "status": "COMPLETE",
            "runId": contract["runId"],
            "commit": environment["commit"],
            "tree": environment["tree"],
            "repository": dict(environment["repository"]),
            "contractSha256": core.sha256_file(contract_path),
            "routeAssessmentSha256": core.sha256_file(
                run_dir / "route-assessment.json"
            ),
            "source": {
                "path": "inputs/candidate.onnx",
                "sha256": core.sha256_file(source_path),
            },
            "toolchain": environment["toolchain"],
            "stages": {
                "onnxPreflight": {
                    "status": "PASS",
                    "report": "verification/onnx-check.json",
                    "reportSha256": core.sha256_file(preflight_path),
                },
                "transform": {"status": "PASS"},
                "deploy": {"status": "PASS"},
                "modelInfo": {
                    "status": "PASS",
                    "contractMatches": True,
                },
                "tensorCompare": {
                    "status": tensor_status,
                    "detail": "synthetic tensor result",
                    "tolerance": "0.99,0.90",
                },
            },
            "artifacts": [
                {
                    "path": "artifacts/candidate_320_bm1688_f16.bmodel",
                    "sha256": core.sha256_file(artifact_path),
                    "sizeBytes": artifact_path.stat().st_size,
                    "role": "bm1688-bmodel",
                }
            ],
            "commands": [
                "docker run --password [REDACTED] synthetic",
            ],
        }
        (run_dir / "execution-manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
        return run_dir, contract_path, contract

    def test_development_and_promotion_verdicts_are_independent(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            run_dir, contract_path, contract = self._make_verifiable_run(root, "UNVERIFIED")
            evidence = conversion.verify_conversion(contract_path, run_dir, contract)
            evidence_schema = json.loads(
                (ROOT / "test" / "agent" / "schemas" / "evidence-v1.schema.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertFalse(set(evidence_schema["required"]) - set(evidence))
            self.assertEqual(evidence["developmentVerdict"], "PARTIAL")
            self.assertEqual(evidence["promotionVerdict"], "NOT_REQUESTED")
            self.assertEqual(evidence["deviceVerdict"], "NOT_RUN")
            self.assertEqual(len(evidence["attempts"]), 1)
            self.assertEqual(evidence["dataFlow"]["status"], "UNVERIFIED")
            self.assertTrue((run_dir / "evidence.md").is_file())

            manifest_path = run_dir / "execution-manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["stages"]["tensorCompare"]["status"] = "PASS"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            complete = conversion.verify_conversion(contract_path, run_dir, contract)
            self.assertEqual(complete["developmentVerdict"], "COMPLETE")
            self.assertEqual(complete["promotionVerdict"], "NOT_REQUESTED")

            (run_dir / "inputs" / "candidate.onnx").write_bytes(b"changed-after-conversion")
            tampered = conversion.verify_conversion(contract_path, run_dir, contract)
            self.assertEqual(tampered["developmentVerdict"], "FAILED")
            self.assertEqual(
                next(stage for stage in tampered["stages"] if stage["id"] == "S1")["status"],
                "FAIL",
            )

    def test_failed_tensor_attempt_remains_visible_until_a_pass_supersedes_it(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            run_dir, contract_path, contract = self._make_verifiable_run(root, "UNVERIFIED")
            manifest_path = run_dir / "execution-manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["attempt"] = 2
            manifest["previousAttempts"] = [
                {
                    "attempt": 1,
                    "status": "FAILED",
                    "stageStatuses": {"tensorCompare": "FAIL"},
                }
            ]
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            evidence = conversion.verify_conversion(contract_path, run_dir, contract)
            tensor = next(stage for stage in evidence["stages"] if stage["id"] == "S3")
            self.assertEqual(tensor["status"], "FAIL")
            self.assertIn("attempt(s) 1", tensor["detail"])

            manifest["stages"]["tensorCompare"]["status"] = "PASS"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            superseded = conversion.verify_conversion(contract_path, run_dir, contract)
            tensor = next(stage for stage in superseded["stages"] if stage["id"] == "S3")
            self.assertEqual(tensor["status"], "PASS")

    def _recordable_manifest(self, run_dir: Path, recorded_at: str, attempt: int) -> dict:
        manifest_path = run_dir / "execution-manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest.update(
            {
                "attempt": attempt,
                "completedAt": recorded_at,
                "source": {
                    "path": "inputs/candidate.onnx",
                    "sha256": core.sha256_file(run_dir / "inputs" / "candidate.onnx"),
                },
                "target": {},
            }
        )
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        return manifest

    def test_two_recordings_without_seal_remain_candidate(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            example_dir = root / "test" / "agent" / "examples" / "model-conversion"
            example_dir.mkdir(parents=True)
            run_dir, _, contract = self._make_verifiable_run(root, "PASS")
            parameters = conversion.conversion_parameters(contract, run_dir)
            manifest = self._recordable_manifest(
                run_dir, "2026-01-01T00:00:00Z", 1
            )
            example_path = example_dir / "candidate-bm1688-f16.json"
            first = conversion.record_example(
                str(example_path),
                manifest,
                parameters,
                run_dir=run_dir,
                project_root=root,
                emit_summary=False,
            )
            self.assertEqual(first["status"], "candidate")
            self.assertEqual(first["lifecycle"]["status"], "active")
            self.assertEqual(len(first["recordings"]), 1)
            self.assertEqual(first["deviceValidation"]["status"], "none")

            manifest = self._recordable_manifest(
                run_dir, "2026-01-01T00:10:00Z", 2
            )
            device_evidence = root / "device-smoke.txt"
            device_evidence.write_text("Device runtime smoke: PASS\n", encoding="utf-8")
            second = conversion.record_example(
                str(example_path),
                manifest,
                parameters,
                run_dir=run_dir,
                project_root=root,
                device_evidence=str(device_evidence),
                emit_summary=False,
            )
            self.assertEqual(second["status"], "candidate")
            self.assertEqual(len(second["recordings"]), 2)
            self.assertNotIn("seal", second)
            self.assertEqual(second["deviceValidation"]["status"], "referenced")
            self.assertNotIn(str(root), second["deviceValidation"]["reference"])

            outside = root / "candidate-outside.json"
            with self.assertRaisesRegex(core.WorkflowError, "must stay under"):
                conversion.record_example(
                    str(outside),
                    manifest,
                    parameters,
                    run_dir=run_dir,
                    project_root=root,
                    emit_summary=False,
                )

    def test_sealed_second_recording_promotes_conversion_verified(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            example_dir = root / "test" / "agent" / "examples" / "model-conversion"
            example_dir.mkdir(parents=True)
            run_dir, contract_path, contract = self._make_verifiable_run(root, "PASS")
            parameters = conversion.conversion_parameters(contract, run_dir)
            example_path = example_dir / "candidate-bm1688-f16.json"

            manifest = self._recordable_manifest(run_dir, "2026-01-01T00:00:00Z", 1)
            evidence = conversion.verify_conversion(contract_path, run_dir, contract)
            first_seal, reason = conversion.issue_verification_seal(run_dir, evidence)
            self.assertEqual(reason, "")
            self.assertIsNotNone(first_seal)
            first = conversion.record_example(
                str(example_path),
                manifest,
                parameters,
                run_dir=run_dir,
                project_root=root,
                emit_summary=False,
            )
            self.assertEqual(first["status"], "candidate")
            self.assertRegex(first["recordings"][0]["seal"], r"^CE1-[0-9a-f]{12}$")

            manifest = self._recordable_manifest(run_dir, "2026-01-01T00:10:00Z", 2)
            evidence = conversion.verify_conversion(contract_path, run_dir, contract)
            second_seal, reason = conversion.issue_verification_seal(run_dir, evidence)
            self.assertEqual(reason, "")
            second = conversion.record_example(
                str(example_path),
                manifest,
                parameters,
                run_dir=run_dir,
                project_root=root,
                emit_summary=False,
            )
            self.assertEqual(second["status"], "conversion-verified")
            self.assertEqual(second["lifecycle"]["status"], "active")
            self.assertEqual(second["seal"], second_seal["sealCode"])
            self.assertEqual(second_seal["chainStatus"]["conversion"], "PASS")
            self.assertEqual(second_seal["toolchainDigest"], "a" * 12)
            valid, reason, _ = conversion.validate_verification_seal(run_dir)
            self.assertTrue(valid, reason)

            second["lifecycle"] = {
                "status": "revoked",
                "reason": "The recorded compiler route was later found to be defective.",
                "changedAt": "2026-01-02T00:00:00Z",
                "reference": "issue:fixture-toolchain-defect",
            }
            example_path.write_text(json.dumps(second), encoding="utf-8")
            revoked = conversion.verify_conversion(
                contract_path,
                run_dir,
                contract,
                example_path=str(example_path),
                project_root=root,
            )
            self.assertEqual(revoked["promotionVerdict"], "NOT_READY")
            self.assertEqual(revoked["selectedExampleLifecycle"], "revoked")
            valid, reason, _ = conversion.validate_verification_seal(run_dir)
            self.assertTrue(valid, reason)
            with self.assertRaisesRegex(core.WorkflowError, "revoked examples"):
                conversion.record_example(
                    str(example_path),
                    manifest,
                    parameters,
                    run_dir=run_dir,
                    project_root=root,
                    emit_summary=False,
                )

            manifest["completedAt"] = "changed-after-seal"
            (run_dir / "execution-manifest.json").write_text(
                json.dumps(manifest), encoding="utf-8"
            )
            valid, reason, _ = conversion.validate_verification_seal(run_dir)
            self.assertFalse(valid)
            self.assertIn("identity", reason)

    def test_legacy_verified_record_is_normalized_without_error(self):
        normalized = conversion._normalize_example_record(
            {
                "status": "verified",
                "knownLimits": list(conversion.CONVERSION_KNOWN_LIMITS),
            }
        )
        self.assertEqual(normalized["status"], "conversion-verified")
        self.assertEqual(normalized["lifecycle"]["status"], "active")
        self.assertEqual(normalized["deviceValidation"]["status"], "none")

        with self.assertRaisesRegex(core.WorkflowError, "requires reason"):
            conversion._normalize_example_record(
                {
                    "status": "candidate",
                    "lifecycle": {
                        "status": "revoked",
                        "changedAt": "2026-01-02T00:00:00Z",
                    },
                }
            )

    def test_verify_reads_candidate_and_legacy_verified_examples_without_error(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            example_dir = root / "test" / "agent" / "examples" / "model-conversion"
            example_dir.mkdir(parents=True)
            run_dir, contract_path, contract = self._make_verifiable_run(root, "PASS")
            for status in ("candidate", "verified"):
                with self.subTest(status=status):
                    example_path = example_dir / f"{status}.json"
                    example_path.write_text(
                        json.dumps(
                            {
                                "status": status,
                                "exampleId": f"model-conversion/{status}",
                                "applicability": {},
                                "recordings": [],
                                "knownLimits": list(conversion.CONVERSION_KNOWN_LIMITS),
                            }
                        ),
                        encoding="utf-8",
                    )
                    evidence = conversion.verify_conversion(
                        contract_path,
                        run_dir,
                        contract,
                        example_path=str(example_path),
                        project_root=root,
                    )
                    self.assertEqual(evidence["promotionVerdict"], "NOT_READY")

    def test_device_evidence_is_an_opaque_reference_not_a_customer_path(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            evidence_path = root / "customer-device-report.txt"
            evidence_path.write_text("Device runtime smoke: PASS\n", encoding="utf-8")
            reference = conversion._device_evidence_record(str(evidence_path), root)
            self.assertEqual(reference["status"], "referenced")
            self.assertRegex(reference["reference"], r"^external:sha256:[0-9a-f]{64}$")
            self.assertNotIn(str(root), json.dumps(reference))
            self.assertNotIn(evidence_path.name, json.dumps(reference))

            placeholder = root / "placeholder.txt"
            placeholder.write_text("TODO", encoding="utf-8")
            with self.assertRaisesRegex(core.WorkflowError, "placeholder"):
                conversion._device_evidence_record(str(placeholder), root)

    def test_scope_summary_leads_with_limits_and_device_boundary(self):
        output = io.StringIO()
        with redirect_stdout(output):
            conversion._print_scope_summary(
                status="conversion-verified",
                validated_stages=[
                    "onnx-preflight",
                    "transform",
                    "deploy",
                    "model-info",
                    "tensor-compare",
                ],
                known_limits=list(conversion.CONVERSION_KNOWN_LIMITS),
                device_validation={"status": "none"},
                seal_code="CE1-0123456789ab",
                lifecycle_status="active",
            )
        lines = output.getvalue().splitlines()
        self.assertEqual(lines[0], "STATUS: conversion-verified")
        self.assertEqual(lines[1], "LIFECYCLE: active")
        self.assertEqual(
            lines[2],
            "COVERS: ONNX preflight, transform, deploy, model-info, tensor-compare",
        )
        self.assertEqual(
            lines[3],
            "NOT COVERED: device import and runtime; business accuracy acceptance",
        )
        self.assertEqual(lines[4], "DEVICE VALIDATION: none")


if __name__ == "__main__":
    unittest.main()
