"""Effective platform/lock contracts and actual community artifact identities."""
import hashlib
import importlib.util
import json
from pathlib import Path
import unittest

REPOSITORY = Path(__file__).resolve().parents[1]


class PlatformContracts(unittest.TestCase):
    def test_builder_policy_and_installer_identity(self):
        lock = json.loads((REPOSITORY / 'config/rockchip-build/builder-lock.json').read_text())
        rk, rv = lock['targets']['rk3576'], lock['targets']['rv1126b']
        self.assertIs(rk['rkllm_required'], True)
        self.assertIs(rv['rkllm_required'], False)
        self.assertNotEqual(Path(rk['media_root']), Path(rv['media_root']))
        for chip, target in lock['targets'].items():
            self.assertEqual(Path(target['media_root']), Path('/opt/rockchip-media') / chip)
        self.assertIn('share/licenses/rockchip-media/librga/COPYING', rv['required_package_paths'])
        self.assertIn('lib/librkllmrt.so', rv['forbidden_package_paths'])
        self.assertIn('share/licenses/rkllm/LICENSE', rk['required_package_paths'])
        self.assertIn('lib/librkllmrt.so', rk['required_package_paths'])
        spec = importlib.util.spec_from_file_location('rkllm_installer', REPOSITORY / 'scripts/install_rkllm_sdk.py')
        installer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(installer)
        self.assertEqual(lock['common']['rkllm']['version'], '1.3.0')
        self.assertEqual(lock['common']['rkllm']['version'], installer.VERSION)
        self.assertEqual(lock['common']['rkllm']['revision'], installer.COMMIT)
        self.assertRegex(installer.COMMIT, r'^[0-9a-f]{40}$')
        self.assertTrue(installer.SOURCE_BASE.endswith('/' + lock['common']['rkllm']['revision']))

    def test_rknn_platform_profiles_share_backend_and_separate_artifacts(self) -> None:
        rk3576 = json.loads(
            (REPOSITORY / "config/rknn/platforms/rk3576.json").read_text(
                encoding="utf-8"
            )
        )
        rv1126b = json.loads(
            (REPOSITORY / "config/rknn/platforms/rv1126b.json").read_text(
                encoding="utf-8"
            )
        )
        toolchain_lock = json.loads(
            (REPOSITORY / "config/rknn/toolchain-lock.json").read_text(
                encoding="utf-8"
            )
        )
        builder_lock = json.loads(
            (REPOSITORY / "config/rockchip-build/builder-lock.json").read_text(
                encoding="utf-8"
            )
        )
        runtime_lock = json.loads(
            (REPOSITORY / "config/rockchip-media/runtime-lock.json").read_text(
                encoding="utf-8"
            )
        )
        for profile, chip in ((rk3576, "rk3576"), (rv1126b, "rv1126b")):
            self.assertEqual(profile["backend"], "rknn")
            self.assertEqual(profile["chip"], chip)
            self.assertEqual(profile["conversion"]["target_platform"], chip)
            self.assertTrue(profile["media"]["cpu_fallback"])
            self.assertEqual(profile["media"]["default_backend"], "rockchip")
            self.assertEqual(
                profile["media"]["runtime_lock"],
                "../../rockchip-media/runtime-lock.json",
            )
            self.assertTrue(profile["qualification"]["requires_target_bound_evidence"])
            self.assertEqual(
                profile["qualification"]["status"],
                toolchain_lock["qualification"][chip]["status"],
            )
        self.assertNotEqual(
            rk3576["media"]["runtime_profile"],
            rv1126b["media"]["runtime_profile"],
        )
        self.assertNotEqual(
            rk3576["packaging"]["legacy_models_directory"],
            rv1126b["packaging"]["legacy_models_directory"],
        )
        rv_artifact_manifest_path = (
            REPOSITORY / rv1126b["packaging"]["artifact_manifest"]
        )
        rv_artifact_manifest = json.loads(
            rv_artifact_manifest_path.read_text(encoding="utf-8")
        )
        self.assertEqual(rv_artifact_manifest["chip"], "rv1126b")
        self.assertEqual(
            rv_artifact_manifest["usage_scope"], "community-example"
        )
        self.assertFalse(rv_artifact_manifest["commercial_delivery"])
        self.assertEqual(
            {record["model"] for record in rv_artifact_manifest["models"]},
            {"helmet", "yolov8"},
        )
        for record in rv_artifact_manifest["models"]:
            artifact = record["artifact"]
            artifact_path = REPOSITORY / artifact["path"]
            self.assertEqual(artifact_path.stat().st_size, artifact["size_bytes"])
            self.assertEqual(
                hashlib.sha256(artifact_path.read_bytes()).hexdigest(),
                artifact["sha256"],
            )
        for profile in (rk3576, rv1126b):
            chip = profile["chip"]
            self.assertEqual(
                builder_lock["targets"][chip]["media_runtime_profile"],
                profile["media"]["runtime_profile"],
            )
            self.assertIn(
                profile["media"]["runtime_profile"], runtime_lock["runtimes"]
            )

if __name__ == "__main__":
    unittest.main()
