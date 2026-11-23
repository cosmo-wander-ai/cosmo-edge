#!/usr/bin/env python3
"""Run the production resource install rules in a compiler-free CMake fixture.

The fixture executes the resource block from CMakeLists.txt verbatim. Assertions
inspect installed files, so regressions in CMake matching and overlay order are
observable without compiling the engine or duplicating the install rules.
"""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


REPOSITORY = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which("cmake"), "CMake is required")
class ResourceInstallTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="cosmo-resource-install-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root / "source"
        self.source.mkdir()
        self.base = self.source / "base resources"
        self.overlay = self.source / "staged overlay"
        self.bundle = {
            "schema_version": 1,
            "chip": "rk3588",
            "models": [{"model": "fixture", "package_directory": "fixture"}],
        }
        files = {
            self.base / "algorithms/base.json": b"base algorithm",
            self.base / "models/old/model.onnx": b"unselected model",
            self.base / "model-bundle.json": b'{"source":"old-base"}',
            self.overlay / "algorithms/overlay.json": b"overlay algorithm",
            self.overlay / "models/fixture/model.rknn": b"staged RKNN model",
            self.overlay / "model-bundle.json": json.dumps(self.bundle).encode(),
        }
        for path, data in files.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)

        cmake_source = (REPOSITORY / "CMakeLists.txt").read_text()
        start = cmake_source.index("# Resource (models, algorithm, layout, etc. from external repository)")
        end = cmake_source.index("\nif(COSMO_NN_USE_RKNN_BACKEND)\n", start)
        (self.source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.16)\n"
            "project(resource_install_contract NONE)\n"
            'set(RESOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/base resources")\n'
            'set(RESOURCE_OVERLAY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/staged overlay")\n'
            'set(RESOURCE_MODELS_DIR "${RESOURCE_OVERLAY_DIR}/models")\n'
            + cmake_source[start:end]
        )

    def install(self, policy, profile="public-runtime"):
        build = self.root / "build"
        installed = self.root / "installed"
        subprocess.run([
            "cmake", "-S", str(self.source), "-B", str(build),
            "-DCMAKE_INSTALL_PREFIX=" + str(installed),
            "-DCOSMO_PACKAGE_MODELS=" + policy,
            "-DCOSMO_MODEL_GUARD_BUILD_PROFILE=" + profile,
        ], check=True, capture_output=True, text=True)
        subprocess.run(["cmake", "--install", str(build)], check=True,
                       capture_output=True, text=True)
        resource = installed / "resource"
        self.assertEqual((resource / "algorithms/base.json").read_bytes(), b"base algorithm")
        self.assertEqual((resource / "algorithms/overlay.json").read_bytes(), b"overlay algorithm")
        self.assertFalse((resource / "models/old/model.onnx").exists())
        return resource

    def test_preserve_omits_weights_and_stale_manifests_from_both_sources(self):
        resource = self.install("preserve")
        self.assertFalse((resource / "models").exists())
        self.assertFalse((resource / "model-bundle.json").exists())

    def test_public_include_keeps_model_bundle_and_selected_weights(self):
        # Normal template roots have no bundle; the staged overlay owns it.
        # Keep the stale base-bundle fixture for the preserve regression only.
        (self.base / "model-bundle.json").unlink()
        resource = self.install("include")
        self.assertEqual(json.loads((resource / "model-bundle.json").read_text()), self.bundle)
        self.assertEqual((resource / "models/fixture/model.rknn").read_bytes(), b"staged RKNN model")

    def test_protected_include_keeps_existing_overlay_bundle_exclusion(self):
        # Protected builds historically exclude the public overlay manifest.
        # Their normal base templates do not carry a model-bundle manifest.
        (self.base / "model-bundle.json").unlink()
        resource = self.install("include", "production-release")
        self.assertFalse((resource / "model-bundle.json").exists())
        self.assertEqual((resource / "models/fixture/model.rknn").read_bytes(), b"staged RKNN model")


if __name__ == "__main__":
    unittest.main()
