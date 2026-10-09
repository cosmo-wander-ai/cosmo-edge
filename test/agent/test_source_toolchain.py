import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import agent_workflow as core
import model_conversion_workflow as conversion
import source_toolchain


class SourceToolchainTest(unittest.TestCase):
    def specification(self, root):
        specification, error = core._toolchain_spec({
            "targetChip": "bm1688",
            "toolchain": {
                "kind": "container-image", "layout": "source-tree",
                "image": "sophgo/tpuc_dev:latest", "sourceRoot": str(root),
                "sourceMount": "/opt/tpu-mlir", "environment": {"OMP_NUM_THREADS": "4"},
            },
        })
        self.assertEqual(error, "")
        return specification

    def fixture(self, root):
        for relative in ("python/tools", "install/bin", "install/lib", "install/python", "lib"):
            (root / relative).mkdir(parents=True)
        (root / "python/tools/model_transform.py").write_text("# synthetic transform fixture\n")
        (root / "python/tools/model_deploy.py").write_text("# synthetic deploy fixture\n")
        (root / "install/python/pymlir.py").write_text("__version__ = '1.0.0.dev-fixture'\n")
        (root / "install/lib/runtime.so").write_bytes(b"synthetic runtime identity fixture")
        (root / "lib/compiler.cc").write_text("// synthetic compiler source fixture\n")
        compiler = root / "install/bin/tpuc-opt"
        compiler.write_text("#!/bin/sh\nprintf 'TPU-MLIR synthetic-test-version\\n'\n")
        compiler.chmod(0o700)

    def probe(self, root):
        environment = os.environ.copy()
        environment["PATH"] = str(root / "install/bin") + os.pathsep + environment.get("PATH", "")
        environment["PYTHONPATH"] = str(root / "install/python")
        environment["PYTHONDONTWRITEBYTECODE"] = "1"
        result = subprocess.run(
            [sys.executable, "-c", source_toolchain.PROBE_SCRIPT, str(root), "{}", "pymlir"],
            text=True, capture_output=True, env=environment,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def test_source_layout_does_not_claim_python_distribution_identity(self):
        specification = self.specification(Path("/existing/compiler"))
        self.assertNotIn("package", specification)
        self.assertEqual(specification["module"], "pymlir")
        self.assertEqual(specification["sourceTree"]["environment"]["PYTHONDONTWRITEBYTECODE"], "1")
        for extra in ({"package": "onnx"}, {"kind": "python-package"}, {"layout": "unknown"},
                      {"sourceRoot": "relative/path"}, {"sourceMount": "/workspace/run"},
                      {"sourceRoot": "/host/path,readonly=false"},
                      {"environment": {"PATH": "$PATH:/compiler"}},
                      {"environment": {"PYTHONSTARTUP": "/execute.py"}}):
            with self.subTest(extra=extra):
                raw = {"kind": "container-image", "layout": "source-tree", "image": "fixture:1",
                       "sourceRoot": "/existing/compiler", **extra}
                result, error = core._toolchain_spec({"targetChip": "bm1688", "toolchain": raw})
                self.assertIsNone(result)
                self.assertTrue(error)

    def test_probe_freezes_real_source_and_runtime_without_record(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            before = self.probe(root)
            self.assertNotIn("package", before)
            self.assertEqual(before["compiler"]["name"], "tpu-mlir")
            self.assertEqual(before["compiler"]["version"], "1.0.0.dev-fixture")
            self.assertIn("install/lib/runtime.so", before["compiler"]["source"]["files"])
            self.assertIn("lib/compiler.cc", before["compiler"]["source"]["files"])
            self.assertFalse((root / "install/python/__pycache__").exists())

            (root / "build").mkdir()
            (root / "build/unrelated-cache").write_bytes(b"not an executing compiler artifact")
            self.assertEqual(before, self.probe(root))
            (root / "install/lib/runtime.so").write_bytes(b"changed runtime")
            changed = self.probe(root)
            self.assertNotEqual(before["compiler"]["source"]["contentSha256"],
                                changed["compiler"]["source"]["contentSha256"])
            (root / "lib/compiler.cc").write_text("// changed compiler source\n")
            self.assertNotEqual(changed["compiler"]["source"]["contentSha256"],
                                self.probe(root)["compiler"]["source"]["contentSha256"])

    def test_probe_requires_tools_and_rejects_path_shadowing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            environment = os.environ.copy()
            environment["PYTHONPATH"] = str(root / "install/python")
            environment["PYTHONDONTWRITEBYTECODE"] = "1"
            environment["PATH"] = "/usr/bin:/bin"
            result = subprocess.run(
                [sys.executable, "-c", source_toolchain.PROBE_SCRIPT, str(root), "{}", "pymlir"],
                text=True, capture_output=True, env=environment,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("PATH does not resolve tpuc-opt", result.stderr)

    def test_probe_and_conversion_use_same_readonly_mount_environment_and_image(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            specification = self.specification(root)
            payload = self.probe(root)
            image = {"id": "sha256:" + "a" * 64, "reference": "fixture:1"}
            process = subprocess.CompletedProcess([], 0, json.dumps(payload), "")
            with mock.patch.object(core, "_docker_image_identity", return_value=(image, "")), \
                    mock.patch.object(core, "_run", return_value=process) as run:
                identity, error = core.inspect_toolchain(specification)
            self.assertEqual(error, "")
            self.assertNotIn("package", identity)
            probe_command = run.call_args.args[0]
            tool_command = conversion._tool_command(identity, "modelTransform", ["--help"], run_dir=root)
            for command in (probe_command, tool_command):
                self.assertIn(image["id"], command)
                self.assertIn(f"type=bind,src={root},dst=/opt/tpu-mlir,readonly", command)
                self.assertIn("OMP_NUM_THREADS=4", command)
                self.assertIn("PYTHONDONTWRITEBYTECODE=1", command)
                self.assertIn("none", command)
                self.assertNotIn("--privileged", command)
            changed = json.loads(json.dumps(identity))
            changed["sourceTree"]["environment"]["OMP_NUM_THREADS"] = "8"
            changed.pop("id")
            self.assertNotEqual(identity["id"], core._identity_digest(changed))

    def test_source_help_probe_rejects_tracebacks_with_exit_one(self):
        identity = {"kind": "container-image", "layout": "source-tree", "family": "sophon",
                    "pythonExecutable": "/usr/bin/python3", "image": {"id": "sha256:fixture"},
                    "sourceTree": source_toolchain.normalize({"sourceRoot": "/existing/compiler"}),
                    "tools": {"modelTransform": {"path": "/opt/tpu-mlir/python/tools/model_transform.py"},
                              "modelDeploy": {"path": "/opt/tpu-mlir/python/tools/model_deploy.py"}}}
        with mock.patch.object(core, "_run", return_value=subprocess.CompletedProcess(
                [], 1, "", "Traceback (most recent call last): missing compiler module")):
            ready, error = core._toolchain_tools_respond(identity)
        self.assertFalse(ready)
        self.assertIn("did not answer", error)
        with mock.patch.object(core, "_run", return_value=subprocess.CompletedProcess(
                [], 0, "usage: --model_def INPUT --mlir OUTPUT", "")) as run:
            ready, error = core._toolchain_tools_respond(identity)
        self.assertTrue(ready, error)
        self.assertIn("type=bind,src=/existing/compiler,dst=/opt/tpu-mlir,readonly",
                      run.call_args.args[0])

    def test_changed_source_is_rejected_before_verification(self):
        contract = {"runId": "fixture"}
        parameters = {"toolchainSpec": {"layout": "source-tree"}}
        environment = {"toolchain": {"layout": "source-tree", "id": "sha256:admitted"}}
        manifest = {"status": "COMPLETE", "runId": "fixture", "contractSha256": "fixture-hash",
                    "toolchain": environment["toolchain"]}
        with mock.patch.object(conversion, "conversion_parameters", return_value=parameters), \
                mock.patch.object(conversion, "_read_environment_report", return_value=environment), \
                mock.patch.object(conversion, "_read_asset_selection", return_value={}), \
                mock.patch.object(core, "load_json", return_value=manifest), \
                mock.patch.object(core, "sha256_file", return_value="fixture-hash"), \
                mock.patch.object(core, "inspect_toolchain", return_value=({"id": "sha256:changed"}, "")):
            with self.assertRaisesRegex(core.WorkflowError, "changed before verification"):
                conversion.verify_conversion(Path("contract"), Path("run"), contract)


if __name__ == "__main__":
    unittest.main()
