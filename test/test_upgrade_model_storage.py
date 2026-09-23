#!/usr/bin/env python3
"""Exercise model storage reuse and rollback through the real package installer."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


INSTALLER = Path(__file__).resolve().parents[1] / "scripts/legacy_migration_install.sh"


class ModelStorageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="cosmo-model-storage-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.active = self.root / "appfs/cosmo_wander/cwai_data"
        self.payload = self.root / "payload"
        self.models = self.active / "resource/models"
        self.models.mkdir(parents=True)
        (self.active / "bin").mkdir()
        (self.active / "bin/cosmo-engine").write_text("old-engine")
        (self.payload / "scripts").mkdir(parents=True)
        (self.payload / "bin").mkdir()
        (self.payload / "bin/cosmo-engine").write_text("new-engine")
        shutil.copyfile(INSTALLER, self.payload / "scripts/install.sh")
        for name in ("system-log-cleanup.sh", "cosmo-log-cleanup.service", "system-log-retention.py"):
            shutil.copy2(INSTALLER.parent / name, self.payload / "scripts" / name)
        (self.payload / "scripts/system-log-cleanup.sh").chmod(0o755)
        for name in ("stop.sh", "start.sh", "inte_run_start.sh"):
            script = self.payload / "scripts" / name
            script.write_text("#!/bin/sh\nexit 0\n")
            script.chmod(0o755)
        self.keep = self.models / "preserved model.nn"
        # Allocate real blocks: a sparse placeholder would hide the space regression.
        with self.keep.open("wb") as stream:
            for _ in range(32):
                stream.write(b"K" * (1024 * 1024))
        self.keep.chmod(0o444)
        self.original = self.keep.stat()
        self.changed = self.models / "updated.nn"
        self.changed.write_bytes(b"old-model")
        (self.payload / "resource/models").mkdir(parents=True)
        (self.payload / "resource/models/updated.nn").write_bytes(b"new-model")
        (self.payload / "resource/models/new.nn").write_bytes(b"added-model")

    def install(self, **overrides):
        env = dict(os.environ, COSMO_MIGRATION_TEST_ROOT=str(self.root))
        env.pop("CLEAN_RESOURCE", None)
        env.pop("COSMO_MIGRATION_TEST_FAIL_AFTER_ACTIVATION", None)
        env.update(overrides)
        return subprocess.run(
            ["sh", str(self.payload / "scripts/install.sh"), str(self.root / "install.log")],
            env=env, capture_output=True, text=True, check=False,
        )

    def assert_model_reused(self):
        current = self.keep.stat()
        self.assertEqual((current.st_dev, current.st_ino),
                         (self.original.st_dev, self.original.st_ino),
                         "unchanged models must reuse storage, not be copied")
        self.assertEqual(current.st_size, self.original.st_size)
        self.assertEqual(current.st_mode, self.original.st_mode)
        self.assertEqual(current.st_mtime_ns, self.original.st_mtime_ns)
        self.assertEqual(current.st_nlink, 1, "temporary model links must be removed")

    def assert_no_transaction_leftovers(self):
        self.assertEqual(list(self.active.parent.glob(".cosmo-migration-*")), [])

    def test_success_reuses_models_and_replaces_package_updates(self):
        # A second name for the old inode detects in-place writes during overlay.
        alias = self.models / "old-model-alias.nn"
        os.link(self.changed, alias)
        result = self.install()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assert_model_reused()
        self.assertEqual(self.changed.read_bytes(), b"new-model")
        self.assertEqual(alias.read_bytes(), b"old-model")
        self.assertEqual((self.models / "new.nn").read_bytes(), b"added-model")
        self.assertEqual((self.active / "bin/cosmo-engine").read_text(), "new-engine")
        self.assert_no_transaction_leftovers()

    def test_rollback_preserves_old_model_content_and_storage(self):
        changed_inode = self.changed.stat().st_ino
        result = self.install(COSMO_MIGRATION_TEST_FAIL_AFTER_ACTIVATION="1")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("injected post-activation failure", result.stderr)
        self.assert_model_reused()
        self.assertEqual(self.changed.stat().st_ino, changed_inode)
        self.assertEqual(self.changed.read_bytes(), b"old-model")
        self.assertFalse((self.models / "new.nn").exists())
        self.assertEqual((self.active / "bin/cosmo-engine").read_text(), "old-engine")
        self.assert_no_transaction_leftovers()

    def test_package_directory_cannot_write_through_preserved_symlink(self):
        external = self.root / "external-models"
        external.mkdir()
        (external / "model.nn").write_bytes(b"external-original")
        (self.models / "linked").symlink_to(external, target_is_directory=True)
        supplied = self.payload / "resource/models/linked"
        supplied.mkdir()
        (supplied / "model.nn").write_bytes(b"package-replacement")
        result = self.install()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual((external / "model.nn").read_bytes(), b"external-original")
        self.assertEqual((self.active / "bin/cosmo-engine").read_text(), "old-engine")
        self.assert_no_transaction_leftovers()

    def test_clean_resource_still_uses_only_package_models(self):
        result = self.install(CLEAN_RESOURCE="1")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse(self.keep.exists())
        self.assertEqual(self.changed.read_bytes(), b"new-model")
        self.assert_no_transaction_leftovers()


if __name__ == "__main__":
    unittest.main()
