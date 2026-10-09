import io
import json
import os
import subprocess
import stat
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).resolve().parent / "fixtures"
SCHEMAS = Path(__file__).resolve().parent / "schemas"
sys.path.insert(0, str(ROOT / "tools"))

import agent_workflow  # noqa: E402


class AgentWorkflowTest(unittest.TestCase):
    def setUp(self):
        environment = mock.patch.dict(os.environ)
        environment.start()
        self.addCleanup(environment.stop)
        os.environ.pop("COSMO_AGENT_RUNS_ROOT", None)

    def _model_contract(self, run_id: str) -> dict:
        return {
            "schemaVersion": "1.0",
            "runId": run_id,
            "task": "model-conversion",
            "userObjective": "Validate a synthetic BM1688 conversion environment.",
            "expectedDeliverables": ["environment report"],
            "allowedChanges": ["current run"],
            "requiredCapabilities": [],
            "acceptance": {},
            "authority": {"workspace": "isolated fixture"},
            "parameters": {
                "sourceModel": "model.onnx",
                "targetChip": "bm1688",
                "preflight": {
                    "pythonExecutable": sys.executable,
                    "pythonPackages": {},
                },
            },
        }

    def _model_inventory(self) -> dict:
        return {
            "host": {
                "os": "Linux",
                "osRelease": "fixture",
                "architecture": "x86_64",
                "cpuCount": 8,
                "memoryBytes": {"total": 16 * 1024**3, "available": 8 * 1024**3},
                "diskBytes": {"total": 100 * 1024**3, "free": 50 * 1024**3},
            },
            "repository": {
                "commit": "1" * 40,
                "tree": "2" * 40,
                "branch": "fixture",
                "trackedChanges": 0,
                "untrackedFiles": 0,
                "worktreeFingerprint": "3" * 64,
            },
            "tools": {},
        }

    def _write_ready_assessment(self, contract_path: Path, run_dir: Path, contract: dict) -> dict:
        report = {
            "schemaVersion": "1.0",
            "mode": "assessment",
            "task": contract["task"],
            "runId": contract["runId"],
            "contractSha256": agent_workflow.sha256_file(contract_path),
            "needsInput": [],
            "routeVerdict": "READY",
        }
        agent_workflow.atomic_write_json(run_dir / "route-assessment.json", report)
        return report

    def test_contract_schema_and_runtime_validator_share_thin_required_shell(self):
        schema = json.loads((SCHEMAS / "task-contract-v1.schema.json").read_text(encoding="utf-8"))
        self.assertEqual(tuple(schema["required"]), agent_workflow.REQUIRED_CONTRACT_FIELDS)
        contract = json.loads((FIXTURES / "task-contract.valid.json").read_text(encoding="utf-8"))
        validated = agent_workflow.validate_contract(contract)
        self.assertTrue(validated["futureField"]["allowed"])

    def test_contract_rejects_unknown_coarse_authority_grant(self):
        contract = json.loads((FIXTURES / "task-contract.valid.json").read_text(encoding="utf-8"))
        contract["authority"]["grants"] = ["remote-execution", "install-anything"]
        with self.assertRaisesRegex(agent_workflow.WorkflowError, "unknown grants"):
            agent_workflow.validate_contract(contract)

    def test_invalid_contract_is_rejected(self):
        contract = json.loads(
            (FIXTURES / "task-contract.invalid-missing-objective.json").read_text(encoding="utf-8")
        )
        with self.assertRaisesRegex(agent_workflow.WorkflowError, "userObjective"):
            agent_workflow.validate_contract(contract)

    def test_contract_path_must_stay_in_current_run_and_match_run_id(self):
        fixture = json.loads((FIXTURES / "task-contract.valid.json").read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            run_dir = root / "output" / "agent-runs" / fixture["runId"]
            run_dir.mkdir(parents=True)
            contract_path = run_dir / "task-contract.json"
            contract_path.write_text(json.dumps(fixture), encoding="utf-8")
            resolved, resolved_run, _ = agent_workflow.resolve_contract_context(contract_path, root)
            self.assertEqual(resolved, contract_path.resolve())
            self.assertEqual(resolved_run, run_dir.resolve())

            outside = root / "outside.json"
            outside.write_text(json.dumps(fixture), encoding="utf-8")
            with self.assertRaisesRegex(agent_workflow.WorkflowError, "must stay under"):
                agent_workflow.resolve_contract_context(outside, root)

            fixture["runId"] = "different-run"
            contract_path.write_text(json.dumps(fixture), encoding="utf-8")
            with self.assertRaisesRegex(agent_workflow.WorkflowError, "must match"):
                agent_workflow.resolve_contract_context(contract_path, root)

    def test_external_runs_root_supports_start_and_contract_resolution(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "checkout"
            root.mkdir()
            runs_root = Path(directory) / "private-runs"
            source = root / "model.onnx"
            source.write_bytes(b"fixture")

            with (
                mock.patch.dict(os.environ, {"COSMO_AGENT_RUNS_ROOT": str(runs_root)}),
                mock.patch.object(
                    agent_workflow, "host_inventory", return_value=self._model_inventory()
                ),
            ):
                run_dir, contract_path, contract, assessment = agent_workflow.create_task_run(
                    task="model-conversion",
                    objective="Convert a synthetic model.",
                    materials=[source],
                    target_chip="bm1688",
                    run_id="external-run",
                    project_root=root,
                )
                self.assertEqual(run_dir, runs_root / "external-run")
                self.assertEqual(contract["allowedChanges"], [str(run_dir) + "/"])
                self.assertFalse((root / "output" / "agent-runs").exists())
                self.assertEqual(
                    agent_workflow.resolve_contract_context(contract_path, root)[1], run_dir
                )

                with mock.patch.object(
                    agent_workflow,
                    "create_task_run",
                    return_value=(run_dir, contract_path, contract, assessment),
                ):
                    with mock.patch("sys.stdout", new_callable=io.StringIO) as output:
                        agent_workflow.start_main(["--objective", "fixture", "--format", "json"])
                result = json.loads(output.getvalue())
                self.assertEqual(result["runDirectory"], str(run_dir))
                self.assertEqual(result["contract"], str(contract_path))
                self.assertEqual(
                    result["routeAssessment"], str(run_dir / "route-assessment.json")
                )

    def test_run_input_rejects_parent_traversal_and_symlink_escape(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            run_dir = root / "output" / "agent-runs" / "run"
            run_dir.mkdir(parents=True)
            inside = run_dir / "model.onnx"
            inside.write_bytes(b"fixture")
            outside = root / "private.onnx"
            outside.write_bytes(b"fixture")
            self.assertEqual(agent_workflow.resolve_run_input(run_dir, "model.onnx"), inside.resolve())
            with self.assertRaisesRegex(agent_workflow.WorkflowError, "escapes"):
                agent_workflow.resolve_run_input(run_dir, "../../../private.onnx")
            link = run_dir / "link.onnx"
            try:
                link.symlink_to(outside)
            except OSError:
                self.skipTest("symlinks are not available")
            with self.assertRaisesRegex(agent_workflow.WorkflowError, "escapes"):
                agent_workflow.resolve_run_input(run_dir, "link.onnx")

    def test_environment_status_mapping(self):
        cases = json.loads((FIXTURES / "environment-status-cases.json").read_text(encoding="utf-8"))
        for case in cases:
            with self.subTest(case=case["name"]):
                self.assertEqual(agent_workflow.environment_verdict(case["checks"]), case["expected"])

    def test_python_version_requirements_support_exact_and_minimum_without_freezing_all_tasks(self):
        self.assertTrue(agent_workflow._version_satisfies("1.20.1", "1.20.1"))
        self.assertTrue(agent_workflow._version_satisfies("1.20.1", "==1.20.1"))
        self.assertTrue(agent_workflow._version_satisfies("1.26.0", ">=1.20.1"))
        self.assertFalse(agent_workflow._version_satisfies("1.19.0", ">=1.20.1"))
        self.assertTrue(agent_workflow._version_satisfies("9.9.9", None))

    def test_toolchain_python_keeps_virtual_environment_entry_path(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            interpreter = root / "python-real"
            interpreter.write_text("#!/bin/sh\n", encoding="utf-8")
            link = root / "venv-python"
            try:
                link.symlink_to(interpreter)
            except OSError:
                self.skipTest("symlinks are not available")
            self.assertEqual(agent_workflow._resolve_executable(str(link)), str(link))

    def test_toolchain_probe_rejects_broken_runtime_links(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / "fixture_toolchain"
            (package / "lib").mkdir(parents=True)
            (package / "__init__.py").write_text("", encoding="utf-8")
            broken = package / "lib" / "libcmodel.so"
            try:
                broken.symlink_to(root / "missing-runtime.so")
            except OSError:
                self.skipTest("symlinks are not available")
            metadata = root / "fixture_toolchain-1.0.dist-info"
            metadata.mkdir()
            (metadata / "METADATA").write_text(
                "Metadata-Version: 2.1\nName: fixture_toolchain\nVersion: 1.0\n",
                encoding="utf-8",
            )
            (metadata / "RECORD").write_text(
                "fixture_toolchain/__init__.py,,\n"
                "fixture_toolchain-1.0.dist-info/RECORD,,\n",
                encoding="utf-8",
            )
            environment = os.environ.copy()
            environment["PYTHONPATH"] = str(root)
            process = subprocess.run(
                [
                    sys.executable,
                    "-c",
                    agent_workflow.TOOLCHAIN_PROBE_SCRIPT,
                    "fixture_toolchain",
                ],
                text=True,
                capture_output=True,
                check=False,
                env=environment,
            )
            self.assertNotEqual(process.returncode, 0)
            self.assertIn("broken required links", process.stderr)

    def test_toolchain_probe_accepts_commands_outside_python_bin(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / "fixture_toolchain"
            package.mkdir()
            (package / "__init__.py").write_text("", encoding="utf-8")
            metadata = root / "fixture_toolchain-2.0.dist-info"
            metadata.mkdir()
            (metadata / "METADATA").write_text(
                "Metadata-Version: 2.1\nName: fixture_toolchain\nVersion: 2.0\n",
                encoding="utf-8",
            )
            (metadata / "RECORD").write_text(
                "fixture_toolchain/__init__.py,,\n"
                "fixture_toolchain-2.0.dist-info/RECORD,,\n",
                encoding="utf-8",
            )
            tools = root / "separate-tools"
            tools.mkdir()
            transform = tools / "custom-transform"
            deploy = tools / "custom-deploy"
            transform.write_text("#!/bin/sh\n", encoding="utf-8")
            deploy.write_text("#!/bin/sh\n", encoding="utf-8")
            environment = os.environ.copy()
            environment["PYTHONPATH"] = str(root)
            process = subprocess.run(
                [
                    sys.executable,
                    "-c",
                    agent_workflow.TOOLCHAIN_PROBE_SCRIPT,
                    "fixture_toolchain",
                    json.dumps(
                        {
                            "modelTransform": str(transform),
                            "modelDeploy": str(deploy),
                        }
                    ),
                ],
                text=True,
                capture_output=True,
                check=False,
                env=environment,
            )
            self.assertEqual(process.returncode, 0, process.stderr)
            identity = json.loads(process.stdout)
            self.assertEqual(identity["tools"]["modelTransform"]["resolution"], "declared")
            self.assertEqual(identity["tools"]["modelTransform"]["invocation"], "direct")
            self.assertEqual(identity["tools"]["modelDeploy"]["path"], str(deploy.resolve()))

    def test_unspecified_toolchain_selects_capability_probe_without_version_pin(self):
        specification, error = agent_workflow._toolchain_spec({})
        self.assertEqual(error, "")
        self.assertEqual(specification["kind"], "auto")
        self.assertIsNone(specification["version"])
        self.assertIsNone(specification["pythonExecutable"])
        self.assertEqual(
            specification["officialReference"],
            agent_workflow.TPU_MLIR_OFFICIAL_REFERENCE,
        )
        invalid, error = agent_workflow._toolchain_spec({"toolchain": "fixed-recipe"})
        self.assertIsNone(invalid)
        self.assertIn("must be an object", error)

    def test_rknn_target_selects_rknn_toolkit2_package_and_module(self):
        specification, error = agent_workflow._toolchain_spec(
            {"targetChip": "rv1126b"}
        )
        self.assertEqual(error, "")
        self.assertEqual(specification["family"], "rknn")
        self.assertEqual(specification["package"], "rknn-toolkit2")
        self.assertEqual(specification["module"], "rknn.api")
        self.assertEqual(
            specification["officialReference"],
            agent_workflow.RKNN_TOOLKIT2_OFFICIAL_REFERENCE,
        )

    def test_target_chip_can_be_inferred_without_limiting_user_to_example_chips(self):
        contract = self._model_contract("other-chip")
        contract["parameters"].pop("targetChip")
        contract["userObjective"] = "Run this model on our BM1684X test target."
        self.assertEqual(agent_workflow._objective_target_chip(contract), "bm1684x")

    def test_memory_probe_tolerates_windows_missing_sysconf(self):
        with mock.patch.object(
            agent_workflow.os,
            "sysconf",
            side_effect=AttributeError("missing"),
            create=True,
        ):
            total, available = agent_workflow._memory_bytes()
        self.assertTrue(total is None or isinstance(total, int))
        self.assertTrue(available is None or isinstance(available, int))

    def test_private_json_write_tolerates_windows_missing_fchmod(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "private.json"
            with mock.patch.object(agent_workflow.os, "fchmod", None, create=True):
                agent_workflow.atomic_write_json(path, {"status": "PASS"})
            self.assertEqual(json.loads(path.read_text(encoding="utf-8"))["status"], "PASS")

    def test_assessment_asks_only_for_missing_business_input(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract = self._model_contract("business-assessment")
            contract["parameters"].pop("targetChip")
            contract["userObjective"] = "让这个检测模型可以在我的测试设备上运行，并交付转换证据。"
            run_dir = root / "output" / "agent-runs" / contract["runId"]
            run_dir.mkdir(parents=True)
            (run_dir / "model.onnx").write_bytes(b"fixture")
            contract_path = run_dir / "task-contract.json"
            contract_path.write_text(json.dumps(contract), encoding="utf-8")
            with mock.patch.object(
                agent_workflow,
                "host_inventory",
                return_value=self._model_inventory(),
            ):
                report = agent_workflow.assess_task_report(
                    contract_path, run_dir, contract, project_root=root
                )
            self.assertEqual(report["routeVerdict"], "NEEDS_INPUT")
            self.assertEqual([item["id"] for item in report["needsInput"]], ["target-device"])
            questions = " ".join(item["question"] for item in report["needsInput"])
            self.assertNotRegex(questions.lower(), r"python|toolchain|version|quant")
            self.assertEqual(report["recommendedRoute"], "local-linux-tpu-mlir")

    def test_windows_assessment_routes_to_linux_without_calling_windows_unsupported(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract = self._model_contract("windows-assessment")
            run_dir = root / "output" / "agent-runs" / contract["runId"]
            run_dir.mkdir(parents=True)
            (run_dir / "model.onnx").write_bytes(b"fixture")
            contract_path = run_dir / "task-contract.json"
            contract_path.write_text(json.dumps(contract), encoding="utf-8")
            inventory = self._model_inventory()
            inventory["host"].update({"os": "Windows", "architecture": "AMD64"})
            with mock.patch.object(agent_workflow, "host_inventory", return_value=inventory):
                report = agent_workflow.assess_task_report(
                    contract_path, run_dir, contract, project_root=root
                )
            self.assertEqual(report["routeVerdict"], "NEEDS_ENVIRONMENT")
            self.assertEqual(report["recommendedRoute"], "remote-linux-tpu-mlir")
            details = " ".join(item["detail"] for item in report["routeCandidates"])
            self.assertIn("不等于 CosmoEdge 不支持 Windows", details)

    def test_rv1126b_assessment_routes_to_remote_rknn_toolkit2(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract = self._model_contract("rv1126b-assessment")
            contract["parameters"]["targetChip"] = "rv1126b"
            contract["parameters"]["developmentEnvironment"] = {
                "os": "linux",
                "architecture": "x86_64",
                "reference": "isolated development host",
            }
            contract["authority"]["grants"] = ["remote-execution", "model-transfer"]
            run_dir = root / "output" / "agent-runs" / contract["runId"]
            run_dir.mkdir(parents=True)
            (run_dir / "model.onnx").write_bytes(b"fixture")
            contract_path = run_dir / "task-contract.json"
            contract_path.write_text(json.dumps(contract), encoding="utf-8")
            inventory = self._model_inventory()
            inventory["host"].update({"os": "Darwin", "architecture": "arm64"})
            with mock.patch.object(agent_workflow, "host_inventory", return_value=inventory):
                report = agent_workflow.assess_task_report(
                    contract_path, run_dir, contract, project_root=root
                )
            self.assertEqual(report["routeVerdict"], "READY")
            self.assertEqual(
                report["recommendedRoute"], "remote-linux-rknn-toolkit2"
            )
            self.assertEqual(
                report["routeCandidates"][-1]["officialReference"],
                agent_workflow.RKNN_TOOLKIT2_OFFICIAL_REFERENCE,
            )

    def test_remote_linux_assessment_consolidates_missing_authority(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract = self._model_contract("remote-assessment")
            contract["parameters"]["developmentEnvironment"] = {
                "os": "linux",
                "architecture": "x86_64",
                "reference": "customer-provided isolated development host",
            }
            run_dir = root / "output" / "agent-runs" / contract["runId"]
            run_dir.mkdir(parents=True)
            (run_dir / "model.onnx").write_bytes(b"fixture")
            contract_path = run_dir / "task-contract.json"
            contract_path.write_text(json.dumps(contract), encoding="utf-8")
            inventory = self._model_inventory()
            inventory["host"].update({"os": "Windows", "architecture": "AMD64"})
            with mock.patch.object(agent_workflow, "host_inventory", return_value=inventory):
                report = agent_workflow.assess_task_report(
                    contract_path, run_dir, contract, project_root=root
                )
            authority_questions = [
                item for item in report["needsInput"] if item["category"] == "authority"
            ]
            self.assertEqual(len(authority_questions), 1)
            self.assertIn("remote-execution", authority_questions[0]["question"])
            self.assertIn("model-transfer", authority_questions[0]["question"])

    def test_assessment_does_not_choose_between_multiple_model_materials(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract = self._model_contract("multiple-models")
            contract["parameters"].pop("sourceModel")
            run_dir = root / "output" / "agent-runs" / contract["runId"]
            inputs = run_dir / "inputs"
            inputs.mkdir(parents=True)
            (inputs / "candidate-a.onnx").write_bytes(b"a")
            (inputs / "candidate-b.onnx").write_bytes(b"b")
            contract_path = run_dir / "task-contract.json"
            contract_path.write_text(json.dumps(contract), encoding="utf-8")
            with mock.patch.object(
                agent_workflow, "host_inventory", return_value=self._model_inventory()
            ):
                report = agent_workflow.assess_task_report(
                    contract_path, run_dir, contract, project_root=root
                )
            self.assertIn(
                "source-model-selection", [item["id"] for item in report["needsInput"]]
            )

    def test_start_creates_private_agent_owned_run_from_ordinary_intent(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "customer-model.onnx"
            source.write_bytes(b"private-model-fixture")
            with mock.patch.object(
                agent_workflow, "host_inventory", return_value=self._model_inventory()
            ):
                run_dir, contract_path, contract, report = agent_workflow.create_task_run(
                    task="model-conversion",
                    objective="让这个检测模型能在测试设备运行，并交付可复核证据。",
                    materials=[source],
                    target_chip="BM1688",
                    run_id="ordinary-user-start",
                    project_root=root,
                )
            self.assertEqual(report["routeVerdict"], "READY")
            self.assertEqual(contract["parameters"]["sourceModel"], "inputs/customer-model.onnx")
            self.assertNotIn(str(source.parent), contract_path.read_text(encoding="utf-8"))
            copied = run_dir / "inputs" / source.name
            self.assertEqual(copied.read_bytes(), source.read_bytes())
            self.assertEqual(stat.S_IMODE(run_dir.stat().st_mode), 0o700)
            self.assertEqual(stat.S_IMODE(copied.stat().st_mode), 0o600)
            self.assertTrue((run_dir / "route-assessment.json").is_file())

    def test_start_selects_single_model_when_other_materials_are_present(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            model = root / "detector.onnx"
            video = root / "sample.mp4"
            board = root / "board.pdf"
            model.write_bytes(b"model")
            video.write_bytes(b"video")
            board.write_bytes(b"pdf")
            with mock.patch.object(
                agent_workflow, "host_inventory", return_value=self._model_inventory()
            ):
                _, _, contract, report = agent_workflow.create_task_run(
                    task="model-conversion",
                    objective="Convert the detector for RV1126B.",
                    materials=[model, video, board],
                    target_chip="rv1126b",
                    run_id="mixed-material-start",
                    project_root=root,
                )
            self.assertEqual(contract["parameters"]["sourceModel"], "inputs/detector.onnx")
            self.assertEqual(report["routeVerdict"], "READY")

    def test_start_refuses_overwrite_and_pt_cannot_bypass_route_gate(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "training-checkpoint.pt"
            source.write_bytes(b"private-pt-fixture")
            with mock.patch.object(
                agent_workflow, "host_inventory", return_value=self._model_inventory()
            ):
                run_dir, contract_path, contract, report = agent_workflow.create_task_run(
                    task="model-conversion",
                    objective="把现有训练物料适配到 BM1688。",
                    materials=[source],
                    target_chip="bm1688",
                    run_id="pt-route-gate",
                    project_root=root,
                )
            self.assertEqual(report["routeVerdict"], "NEEDS_INPUT")
            self.assertIn("onnx-material", [item["id"] for item in report["needsInput"]])
            with self.assertRaisesRegex(agent_workflow.WorkflowError, "not READY"):
                agent_workflow.task_environment_report(
                    "model-conversion", contract_path, run_dir, contract, project_root=root
                )
            with self.assertRaisesRegex(agent_workflow.WorkflowError, "will not be overwritten"):
                agent_workflow.create_task_run(
                    task="model-conversion",
                    objective="another task",
                    materials=[source],
                    target_chip="bm1688",
                    run_id="pt-route-gate",
                    project_root=root,
                )

    def test_remote_route_requires_explicit_grants_and_authorization_invalidates_assessment(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "candidate.onnx"
            source.write_bytes(b"private-onnx-fixture")
            inventory = self._model_inventory()
            inventory["host"].update({"os": "Windows", "architecture": "AMD64"})
            with mock.patch.object(agent_workflow, "host_inventory", return_value=inventory):
                run_dir, contract_path, contract, report = agent_workflow.create_task_run(
                    task="model-conversion",
                    objective="让模型在隔离测试设备运行。",
                    materials=[source],
                    target_chip="bm1688",
                    remote_linux=True,
                    run_id="remote-authority",
                    project_root=root,
                )
            authority = next(
                item for item in report["needsInput"] if item["id"] == "route-authority"
            )
            self.assertIn("remote-execution", authority["question"])
            self.assertIn("model-transfer", authority["question"])
            with self.assertRaisesRegex(agent_workflow.WorkflowError, "explicit user confirmation"):
                agent_workflow.record_authority_grants(
                    contract_path,
                    run_dir,
                    contract,
                    grants=["remote-execution"],
                    confirmed_by_user=False,
                )
            record = agent_workflow.record_authority_grants(
                contract_path,
                run_dir,
                contract,
                grants=["remote-execution", "model-transfer"],
                confirmed_by_user=True,
                target_reference="isolated Linux development host",
            )
            self.assertFalse(record["credentialMaterialStored"])
            with self.assertRaisesRegex(agent_workflow.WorkflowError, "changed after route assessment"):
                agent_workflow.read_route_assessment(contract_path, run_dir, contract)
            with mock.patch.object(agent_workflow, "host_inventory", return_value=inventory):
                refreshed = agent_workflow.assess_task_report(
                    contract_path, run_dir, contract, project_root=root
                )
            agent_workflow.atomic_write_json(run_dir / "route-assessment.json", refreshed)
            self.assertEqual(refreshed["routeVerdict"], "READY")

            direct_objective = (
                "连接隔离开发机检查环境，地址 "
                + ".".join(("192", "168", "50", "20"))
                + "，账号example-user，密码example-secret。"
            )
            with mock.patch.object(agent_workflow, "host_inventory", return_value=inventory):
                _, direct_contract_path, direct_contract, direct_report = (
                    agent_workflow.create_task_run(
                        task="model-conversion",
                        objective=direct_objective,
                        materials=[source],
                        target_chip="bm1688",
                        remote_linux=True,
                        user_requested_remote_access=True,
                        run_id="remote-direct-request",
                        project_root=root,
                    )
                )
            self.assertEqual(direct_report["routeVerdict"], "READY")
            self.assertEqual(
                set(direct_contract["authority"]["grants"]),
                {"remote-execution", "model-transfer"},
            )
            serialized = direct_contract_path.read_text(encoding="utf-8")
            self.assertIn("[PRIVATE_TARGET]", serialized)
            self.assertIn("[REDACTED]", serialized)
            self.assertNotIn("example-secret", serialized)
            self.assertFalse(
                any(item["id"] == "route-authority" for item in direct_report["needsInput"])
            )

    def test_authority_record_sanitizes_connection_material_without_blocking(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract = self._model_contract("credential-sanitization")
            run_dir = root / "output" / "agent-runs" / contract["runId"]
            run_dir.mkdir(parents=True)
            contract_path = run_dir / "task-contract.json"
            contract_path.write_text(json.dumps(contract), encoding="utf-8")
            url_record = agent_workflow.record_authority_grants(
                contract_path,
                run_dir,
                contract,
                grants=["remote-execution"],
                confirmed_by_user=True,
                target_reference="ssh://user:password@example.invalid",
            )
            account_record = agent_workflow.record_authority_grants(
                contract_path,
                run_dir,
                contract,
                grants=["remote-execution"],
                confirmed_by_user=True,
                target_reference="测试环境账号 example-user",
            )
            address_record = agent_workflow.record_authority_grants(
                contract_path,
                run_dir,
                contract,
                grants=["remote-execution"],
                confirmed_by_user=True,
                target_reference="isolated host " + ".".join(("192", "168", "10", "20")),
            )
            self.assertIn("[REDACTED]", url_record["targetReference"])
            self.assertIn("[REDACTED]", account_record["targetReference"])
            self.assertIn("[PRIVATE_TARGET]", address_record["targetReference"])
            serialized = contract_path.read_text(encoding="utf-8")
            self.assertNotIn("example-user", serialized)
            self.assertNotIn("user:password", serialized)
            ordinary = "Add username and password validation fields to the local form."
            self.assertEqual(
                agent_workflow._safe_record_text(ordinary, "objective"), ordinary
            )

    def test_command_redaction(self):
        cases = json.loads((FIXTURES / "redaction-cases.json").read_text(encoding="utf-8"))
        for case in cases:
            with self.subTest(value=case["input"]):
                self.assertEqual(agent_workflow.redact_text(case["input"]), case["expected"])
        nested = {
            "authority": {
                "workspace": "current checkout",
                "credentialReference": "example-only",
                "note": "curl https://example-user:example-pass@example.invalid",
            }
        }
        self.assertEqual(agent_workflow.redact_data(nested)["authority"]["credentialReference"], "[REDACTED]")
        self.assertNotIn("example-pass", agent_workflow.redact_data(nested)["authority"]["note"])

    def test_baseline_is_inventory_not_task_readiness(self):
        process = subprocess.run(
            [str(ROOT / "scripts" / "agent" / "doctor.sh"), "--baseline", "--format", "json"],
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(process.returncode, 0, process.stderr)
        report = json.loads(process.stdout)
        self.assertEqual(report["mode"], "baseline")
        self.assertNotIn("environmentVerdict", report)
        self.assertFalse(report["authority"]["environmentChanges"])
        self.assertFalse(report["authority"]["externalSystems"])

    def test_legacy_base_image_is_repository_owned_not_ready(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract = self._model_contract("legacy-base-image")
            contract["parameters"]["toolchainImage"] = "sophgo/tpuc_dev:v3.2"
            run_dir = root / "output" / "agent-runs" / contract["runId"]
            run_dir.mkdir(parents=True)
            (run_dir / "model.onnx").write_bytes(b"fixture")
            contract_path = run_dir / "task-contract.json"
            contract_path.write_text(json.dumps(contract), encoding="utf-8")
            self._write_ready_assessment(contract_path, run_dir, contract)
            with (
                mock.patch.object(
                    agent_workflow,
                    "host_inventory",
                    return_value=self._model_inventory(),
                ),
                mock.patch.object(
                    agent_workflow,
                    "_python_environment_check",
                    return_value=(True, "fixture"),
                ),
            ):
                report = agent_workflow.task_environment_report(
                    "model-conversion",
                    contract_path,
                    run_dir,
                    contract,
                    project_root=root,
                )
            check = next(item for item in report["checks"] if item["id"] == "C5")
            self.assertEqual(report["environmentVerdict"], "REPAIRABLE")
            self.assertEqual(check["owner"], "repository")
            self.assertIn("only an execution image", check["detail"])
            self.assertIsNone(report["toolchain"])

    def test_complete_python_package_toolchain_does_not_require_docker(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract = self._model_contract("python-toolchain")
            contract["parameters"]["toolchain"] = {
                "kind": "python-package",
                "pythonExecutable": sys.executable,
                "package": "tpu_mlir",
                "version": "1.28.1",
            }
            run_dir = root / "output" / "agent-runs" / contract["runId"]
            run_dir.mkdir(parents=True)
            (run_dir / "model.onnx").write_bytes(b"fixture")
            contract_path = run_dir / "task-contract.json"
            contract_path.write_text(json.dumps(contract), encoding="utf-8")
            self._write_ready_assessment(contract_path, run_dir, contract)
            identity = {
                "kind": "python-package",
                "id": "sha256:" + "a" * 64,
                "package": {"name": "tpu_mlir", "version": "1.28.1"},
            }
            with (
                mock.patch.object(
                    agent_workflow,
                    "host_inventory",
                    return_value=self._model_inventory(),
                ),
                mock.patch.object(
                    agent_workflow,
                    "inspect_toolchain",
                    return_value=(identity, ""),
                ),
                mock.patch.object(
                    agent_workflow,
                    "_toolchain_tools_respond",
                    return_value=(True, ""),
                ),
                mock.patch.object(
                    agent_workflow,
                    "_python_environment_check",
                    return_value=(True, "fixture"),
                ),
            ):
                report = agent_workflow.task_environment_report(
                    "model-conversion",
                    contract_path,
                    run_dir,
                    contract,
                    project_root=root,
                )
            checks = {item["id"]: item for item in report["checks"]}
            self.assertEqual(report["environmentVerdict"], "READY")
            self.assertEqual(checks["C4"]["status"], "SKIP")
            self.assertEqual(checks["C5"]["status"], "PASS")
            self.assertEqual(report["toolchain"]["id"], identity["id"])

    def test_doctor_discovers_and_freezes_actual_toolchain_without_exact_version(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract = self._model_contract("auto-toolchain")
            run_dir = root / "output" / "agent-runs" / contract["runId"]
            run_dir.mkdir(parents=True)
            (run_dir / "model.onnx").write_bytes(b"fixture")
            contract_path = run_dir / "task-contract.json"
            contract_path.write_text(json.dumps(contract), encoding="utf-8")
            self._write_ready_assessment(contract_path, run_dir, contract)
            identity = {
                "kind": "python-package",
                "id": "sha256:" + "f" * 64,
                "package": {"name": "tpu_mlir", "version": "1.31.0"},
            }
            with (
                mock.patch.object(
                    agent_workflow, "host_inventory", return_value=self._model_inventory()
                ),
                mock.patch.object(
                    agent_workflow, "inspect_toolchain", return_value=(identity, "")
                ) as inspect,
                mock.patch.object(
                    agent_workflow, "_toolchain_tools_respond", return_value=(True, "")
                ),
                mock.patch.object(
                    agent_workflow, "_python_environment_check", return_value=(True, "fixture")
                ),
            ):
                report = agent_workflow.task_environment_report(
                    "model-conversion", contract_path, run_dir, contract, project_root=root
                )
            specification = inspect.call_args.args[0]
            self.assertEqual(specification["kind"], "auto")
            self.assertIsNone(specification["version"])
            self.assertEqual(report["environmentVerdict"], "READY")
            self.assertEqual(report["toolchain"]["package"]["version"], "1.31.0")

    def test_windows_doctor_returns_linux_environment_guidance_not_traceback(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract = self._model_contract("windows-doctor")
            run_dir = root / "output" / "agent-runs" / contract["runId"]
            run_dir.mkdir(parents=True)
            (run_dir / "model.onnx").write_bytes(b"fixture")
            contract_path = run_dir / "task-contract.json"
            contract_path.write_text(json.dumps(contract), encoding="utf-8")
            self._write_ready_assessment(contract_path, run_dir, contract)
            inventory = self._model_inventory()
            inventory["host"].update({"os": "Windows", "architecture": "AMD64"})
            identity = {
                "kind": "python-package",
                "id": "sha256:" + "a" * 64,
                "package": {"name": "tpu_mlir", "version": "1.31.0"},
            }
            with (
                mock.patch.object(agent_workflow, "host_inventory", return_value=inventory),
                mock.patch.object(
                    agent_workflow, "inspect_toolchain", return_value=(identity, "")
                ) as inspect,
                mock.patch.object(
                    agent_workflow, "_toolchain_tools_respond", return_value=(True, "")
                ),
                mock.patch.object(
                    agent_workflow, "_python_environment_check", return_value=(True, "fixture")
                ),
            ):
                report = agent_workflow.task_environment_report(
                    "model-conversion", contract_path, run_dir, contract, project_root=root
                )
            platform_check = next(item for item in report["checks"] if item["id"] == "C0")
            self.assertEqual(report["environmentVerdict"], "NEEDS_ENVIRONMENT")
            self.assertIn("Windows support elsewhere", platform_check["remediation"])
            inspect.assert_not_called()

    def test_doctor_consolidates_multiple_missing_route_grants(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract = self._model_contract("authority-doctor")
            contract["parameters"].update(
                {"requiresRemoteExecution": True, "requiresModelTransfer": True}
            )
            run_dir = root / "output" / "agent-runs" / contract["runId"]
            run_dir.mkdir(parents=True)
            (run_dir / "model.onnx").write_bytes(b"fixture")
            contract_path = run_dir / "task-contract.json"
            contract_path.write_text(json.dumps(contract), encoding="utf-8")
            self._write_ready_assessment(contract_path, run_dir, contract)
            identity = {
                "kind": "python-package",
                "id": "sha256:" + "a" * 64,
                "package": {"name": "tpu_mlir", "version": "1.31.0"},
            }
            with (
                mock.patch.object(
                    agent_workflow, "host_inventory", return_value=self._model_inventory()
                ),
                mock.patch.object(
                    agent_workflow, "inspect_toolchain", return_value=(identity, "")
                ),
                mock.patch.object(
                    agent_workflow, "_toolchain_tools_respond", return_value=(True, "")
                ),
                mock.patch.object(
                    agent_workflow, "_python_environment_check", return_value=(True, "fixture")
                ),
            ):
                report = agent_workflow.task_environment_report(
                    "model-conversion", contract_path, run_dir, contract, project_root=root
                )
            authority_questions = [
                item for item in report["needsInput"] if item["category"] == "authority"
            ]
            self.assertEqual(len(authority_questions), 1)
            self.assertIn("model-transfer", authority_questions[0]["question"])
            self.assertIn("remote-execution", authority_questions[0]["question"])

    def test_official_base_container_without_package_is_repository_owned(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract = self._model_contract("base-container")
            contract["parameters"]["toolchain"] = {
                "kind": "container-image",
                "image": "sophgo/tpuc_dev:v3.2",
                "package": "tpu_mlir",
                "version": "1.28.1",
            }
            run_dir = root / "output" / "agent-runs" / contract["runId"]
            run_dir.mkdir(parents=True)
            (run_dir / "model.onnx").write_bytes(b"fixture")
            contract_path = run_dir / "task-contract.json"
            contract_path.write_text(json.dumps(contract), encoding="utf-8")
            self._write_ready_assessment(contract_path, run_dir, contract)
            docker_info = subprocess.CompletedProcess(
                ["docker", "info"],
                0,
                "29.1.3\n",
                "",
            )
            with (
                mock.patch.object(
                    agent_workflow,
                    "host_inventory",
                    return_value=self._model_inventory(),
                ),
                mock.patch.object(
                    agent_workflow.shutil,
                    "which",
                    side_effect=lambda name: "/usr/bin/docker" if name == "docker" else None,
                ),
                mock.patch.object(agent_workflow, "_run", return_value=docker_info),
                mock.patch.object(
                    agent_workflow,
                    "inspect_toolchain",
                    return_value=(None, "PackageNotFoundError: tpu_mlir"),
                ),
                mock.patch.object(
                    agent_workflow,
                    "_python_environment_check",
                    return_value=(True, "fixture"),
                ),
            ):
                report = agent_workflow.task_environment_report(
                    "model-conversion",
                    contract_path,
                    run_dir,
                    contract,
                    project_root=root,
                )
            check = next(item for item in report["checks"] if item["id"] == "C5")
            self.assertEqual(report["environmentVerdict"], "REPAIRABLE")
            self.assertEqual(check["owner"], "repository")
            self.assertIn("base development environment", check["detail"])

    def test_compatibility_matrix_passes_repository_backed_bm1688_onnx(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "candidate.onnx"
            source.write_bytes(b"fixture")
            check = agent_workflow.compatibility_matrix_check(
                source,
                "bm1688",
                {"toolchainChip": "BM1688"},
                project_root=ROOT,
            )
        self.assertEqual(check["status"], "PASS")
        self.assertIn("chip-neutral ONNX", check["detail"])

    def test_compatibility_matrix_reports_explicit_artifact_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract = self._model_contract("chip-mismatch")
            contract["parameters"].update(
                {
                    "sourceModel": "package/model.nn",
                    "targetChip": "bm1688",
                    "toolchainChip": "bm1688",
                    "toolchain": {
                        "kind": "python-package",
                        "pythonExecutable": sys.executable,
                        "package": "tpu_mlir",
                    },
                }
            )
            run_dir = root / "output" / "agent-runs" / contract["runId"]
            package_dir = run_dir / "package"
            package_dir.mkdir(parents=True)
            (package_dir / "model.nn").write_bytes(b"fixture")
            (package_dir / "config.json").write_text(
                json.dumps({"chip_type": "CV186X"}), encoding="utf-8"
            )
            contract_path = run_dir / "task-contract.json"
            contract_path.write_text(json.dumps(contract), encoding="utf-8")
            self._write_ready_assessment(contract_path, run_dir, contract)
            identity = {
                "kind": "python-package",
                "id": "sha256:" + "a" * 64,
                "package": {"name": "tpu_mlir", "version": "fixture"},
            }
            with (
                mock.patch.object(
                    agent_workflow, "host_inventory", return_value=self._model_inventory()
                ),
                mock.patch.object(
                    agent_workflow, "inspect_toolchain", return_value=(identity, "")
                ),
                mock.patch.object(
                    agent_workflow, "_toolchain_tools_respond", return_value=(True, "")
                ),
                mock.patch.object(
                    agent_workflow, "_python_environment_check", return_value=(True, "fixture")
                ),
            ):
                report = agent_workflow.task_environment_report(
                    "model-conversion",
                    contract_path,
                    run_dir,
                    contract,
                    project_root=root,
                )
            check = next(
                item for item in report["checks"] if item["id"] == "compatibility-matrix"
            )
            self.assertEqual(report["environmentVerdict"], "REPAIRABLE")
            self.assertEqual(check["status"], "FAIL")
            self.assertIn("targetChip=BM1688", check["detail"])
            self.assertIn("artifact chip=CV186X", check["detail"])

    def test_compatibility_matrix_keeps_missing_facts_unverified(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "candidate.onnx"
            source.write_bytes(b"fixture")
            check = agent_workflow.compatibility_matrix_check(
                source,
                "bm1688",
                {"toolchainChip": "bm1688"},
                project_root=root,
            )
        self.assertEqual(check["status"], "UNVERIFIED")
        self.assertNotIn("_outcome", check)
        self.assertIn("facts are missing", check["detail"])


if __name__ == "__main__":
    unittest.main()
