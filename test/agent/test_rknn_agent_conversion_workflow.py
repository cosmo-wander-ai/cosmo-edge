import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import agent_workflow as core  # noqa: E402
import conversion_workflow_dispatch as dispatch  # noqa: E402
from rknn import agent_conversion_workflow as conversion  # noqa: E402
from rknn import stage_platform_resources as staging  # noqa: E402


def make_contract(run_id: str = "rknn-conversion-test") -> dict:
    return {
        "schemaVersion": "1.0",
        "runId": run_id,
        "task": "model-conversion",
        "userObjective": "Convert the canonical YOLOv8 model for RV1126B.",
        "expectedDeliverables": ["RV1126B RKNN model", "evidence"],
        "allowedChanges": ["current run directory"],
        "requiredCapabilities": [],
        "acceptance": {},
        "authority": {"workspace": "isolated fixture"},
        "parameters": {
            "sourceModel": "inputs/model.onnx",
            "modelName": "yolov8",
            "modelFamily": "YOLOv8 detector",
            "targetBackend": "Rockchip RKNN",
            "targetChip": "rv1126b",
            "toolchainChip": "rv1126b",
            "quantization": "INT8",
            "inputLayout": "NCHW",
            "inputShapes": [[1, 3, 640, 640]],
            "expectedOutputShapes": [[1, 84, 8400]],
            "pixelFormat": "rgb",
            "outputKind": "rknn",
            "toolchain": {
                "kind": "auto",
                "package": "rknn-toolkit2",
                "module": "rknn.api",
                "version": "2.3.2",
            },
            "preflight": {"pythonExecutable": sys.executable, "pythonPackages": {}},
        },
    }


class RknnAgentConversionWorkflowTest(unittest.TestCase):
    def test_source_and_normalized_input_use_distinct_onnx_checks(self):
        source = Path("source.onnx")
        report = Path("report.json")
        checker = conversion._onnx_check_command(
            sys.executable, source, report, checker_only=True
        )
        runtime = conversion._onnx_check_command(
            sys.executable, source, report, checker_only=False
        )
        self.assertIn("--checker-only", checker)
        self.assertNotIn("--checker-only", runtime)
        self.assertEqual(checker[-2:], ["--json", str(report)])
        self.assertEqual(runtime[-2:], ["--json", str(report)])

    def test_rv1126b_uses_shared_rknn_workflow_and_profile(self):
        with tempfile.TemporaryDirectory() as directory:
            run_dir = Path(directory)
            inputs = run_dir / "inputs"
            inputs.mkdir()
            (inputs / "model.onnx").write_bytes(b"fixture")
            video = inputs / "sample.mp4"
            video.write_bytes(b"fixture-video")
            parameters = conversion.conversion_parameters(make_contract(), run_dir)
            self.assertEqual(parameters["profile"]["chip"], "rv1126b")
            self.assertEqual(parameters["profile"]["backend"], "rknn")
            self.assertEqual(parameters["toolchainLock"]["version"], "2.3.2")
            self.assertEqual(parameters["calibrationSource"], video.resolve())

    def test_model_spec_and_platform_profile_are_independent(self):
        contract = make_contract()
        with tempfile.TemporaryDirectory() as directory:
            run_dir = Path(directory)
            (run_dir / "inputs").mkdir()
            (run_dir / "inputs" / "model.onnx").write_bytes(b"fixture")
            (run_dir / "inputs" / "sample.mp4").write_bytes(b"fixture")
            rv = conversion.conversion_parameters(contract, run_dir)
            contract["parameters"]["targetChip"] = "rk3576"
            contract["parameters"]["toolchainChip"] = "rk3576"
            rk = conversion.conversion_parameters(contract, run_dir)
            self.assertEqual(rv["specPath"], rk["specPath"])
            self.assertNotEqual(rv["profilePath"], rk["profilePath"])
            self.assertEqual(rv["toolchainLockPath"], rk["toolchainLockPath"])

    def test_classifier_calibration_declares_shared_person_detector_contract(self):
        contract = make_contract()
        contract["parameters"]["modelName"] = "helmet"
        contract["parameters"]["modelFamily"] = "helmet classifier"
        contract["parameters"]["inputShapes"] = [[1, 3, 224, 224]]
        contract["parameters"]["expectedOutputShapes"] = [[1, 2]]
        with tempfile.TemporaryDirectory() as directory:
            run_dir = Path(directory)
            (run_dir / "inputs").mkdir()
            (run_dir / "inputs" / "model.onnx").write_bytes(b"fixture")
            (run_dir / "inputs" / "sample.mp4").write_bytes(b"fixture")
            parameters = conversion.conversion_parameters(contract, run_dir)
            self.assertEqual(parameters["personDetectorSpec"]["name"], "yolov8")
            self.assertEqual(
                parameters["personDetectorSpecPath"],
                ROOT / "config" / "rknn" / "models" / "yolov8.json",
            )
            self.assertEqual(
                parameters["personDetector"],
                ROOT
                / "data"
                / "resource"
                / "aiboxresource_x86"
                / "models"
                / "prod_X86_9275710_YOLOV8_V1.0.0"
                / "model.onnx",
            )

    def test_dispatch_selects_backend_family_from_contract(self):
        rknn_contract = make_contract()
        sophon_contract = make_contract()
        sophon_contract["parameters"]["targetChip"] = "bm1688"
        with mock.patch.object(
            core,
            "resolve_contract_context",
            return_value=(Path("task-contract.json"), Path("run"), rknn_contract),
        ):
            self.assertEqual(dispatch.workflow_family(["--contract", "fixture"]), "rknn")
        with mock.patch.object(
            core,
            "resolve_contract_context",
            return_value=(Path("task-contract.json"), Path("run"), sophon_contract),
        ):
            self.assertEqual(dispatch.workflow_family(["--contract=fixture"]), "sophon")

    def test_repository_config_cannot_escape_backend_config_root(self):
        contract = make_contract()
        contract["parameters"]["modelSpec"] = "CMakeLists.txt"
        with tempfile.TemporaryDirectory() as directory:
            run_dir = Path(directory)
            (run_dir / "inputs").mkdir()
            (run_dir / "inputs" / "model.onnx").write_bytes(b"fixture")
            (run_dir / "inputs" / "sample.mp4").write_bytes(b"fixture")
            with self.assertRaisesRegex(core.WorkflowError, "must stay under"):
                conversion.conversion_parameters(contract, run_dir)

    def test_resource_staging_rewrites_only_platform_identity(self):
        source = {
            "chip_type": "RK3576",
            "algorithmName": "RK3576 Helmet",
            "algorithmProcessdata": '[{"position":"rk3576-det","atomicCode":"9275710"}]',
            "algorithmCode": 7463001,
        }
        staged = staging.replace_platform_tokens(source, "RK3576", "RV1126B")
        self.assertEqual(staged["chip_type"], "RV1126B")
        self.assertEqual(staged["algorithmName"], "RV1126B Helmet")
        self.assertIn("rv1126b-det", staged["algorithmProcessdata"])
        self.assertEqual(staged["algorithmCode"], 7463001)

    def test_resource_staging_reads_embedded_atomic_contract(self):
        document = {
            "atomicList": json.dumps(
                [
                    {"atomicCode": "9275710"},
                    {"atomicCode": "7982161"},
                ]
            )
        }
        self.assertEqual(
            staging.algorithm_atomic_codes(document), {"9275710", "7982161"}
        )

    def _resource_staging_fixture(self, root: Path) -> tuple[Path, Path, Path]:
        profile_path = root / "config/rknn/platforms/rv1126b.json"
        profile_path.parent.mkdir(parents=True)
        profile_path.write_text(
            json.dumps(
                {
                    "chip": "rv1126b",
                    "backend": "rknn",
                    "packaging": {
                        "directory_token": "RV1126B",
                        "resource_template_directory": "data/resource/template",
                        "resource_overlay_directory": (
                            "output/platform-artifacts/rv1126b/resource-overlay"
                        ),
                        "resource_manifest_required": True,
                    },
                }
            ),
            encoding="utf-8",
        )
        spec_path = root / "config/rknn/models/yolov8.json"
        spec_path.parent.mkdir(parents=True)
        source_path = root / "data/resource/source/yolov8.onnx"
        source_path.parent.mkdir(parents=True)
        source_path.write_bytes(b"onnx-fixture")
        spec_path.write_text(
            json.dumps(
                {
                    "source_repository_path": source_path.relative_to(root).as_posix(),
                    "source_sha256": staging.sha256(source_path),
                    "packaging": {"algorithm_code": "9275710"},
                }
            ),
            encoding="utf-8",
        )
        template_config = (
            root
            / "data/resource/template/models/"
            "prod_RK3576_9275710_YOLOV8_V1.0.0/config.json"
        )
        template_config.parent.mkdir(parents=True)
        template_config.write_text(
            json.dumps(
                {
                    "algorithm_code": "9275710",
                    "chip_type": "RK3576",
                    "models": [
                        {
                            "params": {
                                "rknn_input_contract": "cosmo.rknn.input.rgb_u8.v1"
                            }
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )
        algorithm_path = root / "data/resource/template/algorithm/fixture_RK3576.json"
        algorithm_path.parent.mkdir(parents=True)
        algorithm_path.write_text(
            json.dumps({"atomicList": [{"atomicCode": "9275710"}]}),
            encoding="utf-8",
        )
        artifact_path = root / "artifact.rknn"
        artifact_path.write_bytes(b"rknn-fixture")
        return profile_path, template_config, artifact_path

    def _artifact_manifest_fixture(
        self, root: Path, artifact_path: Path
    ) -> Path:
        spec_path = root / "config/rknn/models/yolov8.json"
        spec = json.loads(spec_path.read_text(encoding="utf-8"))
        source_path = root / spec["source_repository_path"]
        license_path = root / "data/resource/model-artifacts/LICENSES/AGPL-3.0.txt"
        license_path.parent.mkdir(parents=True)
        license_path.write_text("fixture AGPL-3.0 text\n", encoding="utf-8")
        archive_path = (
            root
            / "data/resource/model-artifacts/rv1126b/"
            "prod_RV1126B_9275710_YOLOV8_V1.0.0/model.rknn"
        )
        archive_path.parent.mkdir(parents=True)
        archive_path.write_bytes(artifact_path.read_bytes())
        manifest_path = archive_path.parents[1] / "artifact-manifest.json"
        manifest_path.write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "chip": "rv1126b",
                    "usage_scope": "community-example",
                    "commercial_delivery": False,
                    "license": {
                        "spdx": "AGPL-3.0-only",
                        "path": license_path.relative_to(root).as_posix(),
                        "sha256": staging.sha256(license_path),
                    },
                    "models": [
                        {
                            "model": "yolov8",
                            "package_directory": (
                                "prod_RV1126B_9275710_YOLOV8_V1.0.0"
                            ),
                            "spec": {
                                "path": spec_path.relative_to(root).as_posix(),
                                "sha256": staging.sha256(spec_path),
                            },
                            "source": {
                                "path": source_path.relative_to(root).as_posix(),
                                "sha256": staging.sha256(source_path),
                                "size_bytes": source_path.stat().st_size,
                            },
                            "artifact": {
                                "path": archive_path.relative_to(root).as_posix(),
                                "sha256": staging.sha256(archive_path),
                                "size_bytes": archive_path.stat().st_size,
                            },
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )
        return manifest_path

    def test_staged_resource_manifest_proves_current_source_and_artifact(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            profile_path, _, artifact_path = self._resource_staging_fixture(root)
            with mock.patch.object(staging, "PROJECT_ROOT", root):
                manifest = staging.stage_platform_resources(
                    profile_path,
                    [("yolov8", artifact_path)],
                )
                result = staging.verify_staged_resources(profile_path)
            self.assertEqual(manifest["schema_version"], 2)
            self.assertEqual(result["status"], "PASS")
            self.assertEqual(result["models"], ["yolov8"])
            source_record = manifest["models"][0]["source_template"]
            self.assertTrue(source_record["path"].endswith("config.json"))
            self.assertEqual(len(source_record["sha256"]), 64)

    def test_artifact_manifest_stages_models_license_and_usage_scope(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            profile_path, _, artifact_path = self._resource_staging_fixture(root)
            artifact_manifest = self._artifact_manifest_fixture(root, artifact_path)
            with mock.patch.object(staging, "PROJECT_ROOT", root):
                manifest = staging.stage_platform_resources(
                    profile_path,
                    [],
                    raw_artifact_manifest=artifact_manifest,
                )
                result = staging.verify_staged_resources(profile_path)
            self.assertEqual(result["usage_scope"], "community-example")
            self.assertEqual(
                manifest["artifact_bundle"]["packaged_license"]["spdx"],
                "AGPL-3.0-only",
            )
            output = root / "output/platform-artifacts/rv1126b/resource-overlay"
            self.assertEqual(
                (output / "model-bundle.json").read_bytes(),
                artifact_manifest.read_bytes(),
            )
            self.assertTrue(
                (output / "licenses/model-assets/AGPL-3.0.txt").is_file()
            )

    def test_artifact_manifest_rejects_tampered_archived_model(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            profile_path, _, artifact_path = self._resource_staging_fixture(root)
            artifact_manifest = self._artifact_manifest_fixture(root, artifact_path)
            document = json.loads(artifact_manifest.read_text(encoding="utf-8"))
            archived_model = root / document["models"][0]["artifact"]["path"]
            archived_model.write_bytes(b"tampered")
            with mock.patch.object(staging, "PROJECT_ROOT", root):
                with self.assertRaisesRegex(ValueError, "hash mismatch"):
                    staging.stage_platform_resources(
                        profile_path,
                        [],
                        raw_artifact_manifest=artifact_manifest,
                    )

    def test_staged_resource_verification_rejects_changed_source_config(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            profile_path, template_config, artifact_path = (
                self._resource_staging_fixture(root)
            )
            with mock.patch.object(staging, "PROJECT_ROOT", root):
                staging.stage_platform_resources(
                    profile_path,
                    [("yolov8", artifact_path)],
                )
                config = json.loads(template_config.read_text(encoding="utf-8"))
                config["models"][0]["params"]["rknn_input_contract"] = "changed"
                template_config.write_text(json.dumps(config), encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "hash mismatch"):
                    staging.verify_staged_resources(profile_path)

    def test_staged_resource_verification_rejects_tampered_artifact(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            profile_path, _, artifact_path = self._resource_staging_fixture(root)
            with mock.patch.object(staging, "PROJECT_ROOT", root):
                manifest = staging.stage_platform_resources(
                    profile_path,
                    [("yolov8", artifact_path)],
                )
                staged_artifact = (
                    root
                    / "output/platform-artifacts/rv1126b/resource-overlay"
                    / manifest["models"][0]["artifact"]["path"]
                )
                staged_artifact.write_bytes(b"tampered")
                with self.assertRaisesRegex(ValueError, "hash mismatch"):
                    staging.verify_staged_resources(profile_path)


if __name__ == "__main__":
    unittest.main()
