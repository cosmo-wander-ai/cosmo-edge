#!/usr/bin/python3
"""Regression tests for Open/Protected permanent MD5 package policy."""

from __future__ import annotations

import hashlib
import importlib.util
import io
import json
import pathlib
import shutil
import tarfile
import tempfile
import unittest


REPOSITORY = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "package_verifier", REPOSITORY / "scripts/verify_package_contents.py"
)
assert spec and spec.loader
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)


class PackageProfileTests(unittest.TestCase):
    def make_package(
        self,
        profile: str,
        model: bytes = b"plain-model",
        model_type: str = "yolov8_det",
        target_chip: str | None = None,
        platform_chip: str | None = None,
        platform_runtime: str | None = None,
        runtime_data_dir: str | None = None,
        runtime_app_data_dir: str = "/appfs/cosmo_wander/cwai_data",
        model_bundle_scope: str | None = None,
        include_bundle_license: bool = True,
        omit_required_file: str | None = None,
        include_model_guard: bool | None = None,
        rknn_terms: bytes | None = None,
        model_guard_runtime: bytes | None = None,
        model_file_name: str = "model.nn",
    ) -> pathlib.Path:
        if include_model_guard is None:
            include_model_guard = profile == "production-release"
        root = "cosmo-V1.5.0"
        directory = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, directory)
        initial = directory / f"{root}.tar.gz"
        executable_files = verifier.REQUIRED_EXECUTABLES
        regular_files = verifier.REQUIRED_FILES
        with tarfile.open(initial, "w:gz") as archive:
            root_info = tarfile.TarInfo(root)
            root_info.type = tarfile.DIRTYPE
            root_info.mode = 0o755
            archive.addfile(root_info)
            for name in sorted(verifier.REQUIRED_DIRS):
                info = tarfile.TarInfo(f"{root}/{name}")
                info.type = tarfile.DIRTYPE
                info.mode = 0o755
                archive.addfile(info)
            files = set(executable_files) | set(regular_files)
            files.add("lib/libavcodec.so.58.134.100")
            if target_chip in ("rk3576", "rv1126b"):
                files.update(verifier.RKNN_LICENSE_FILES)
            if include_model_guard:
                files.update(
                    {
                        "lib/libcosmo_model_guard.so.2.0.0",
                        verifier.MODEL_GUARD_TERMS_FILE,
                    }
                )
            if profile == "production-release":
                files.add("bin/cosmo-model-provision")
            rknn_guard = include_model_guard and target_chip == "rk3576"
            guard_payload = b"RKNN Guard ABI1 fixture (not an admitted SDK)"
            header_payload = b"/* RKNN ABI1 fixture */"
            provision_payload = b"#!/bin/sh\n"
            if rknn_guard:
                files.discard(verifier.MODEL_GUARD_RUNTIME_FILE)
                files.update({verifier.MODEL_GUARD_RKNN_RUNTIME_FILE,
                              verifier.MODEL_GUARD_RUNTIME_HASH_FILE,
                              verifier.MODEL_GUARD_RKNN_SDK_MANIFEST_FILE,
                              verifier.MODEL_GUARD_RKNN_HEADER_FILE})
            for name in sorted(files):
                if name == omit_required_file:
                    continue
                if name == "share/cosmo/runtime-paths.env":
                    selected_data_dir = runtime_data_dir or (
                        "/userdata/cwaiuserdata"
                        if target_chip in ("rk3576", "rv1126b")
                        else "/data/cwaiuserdata"
                    )
                    data = (
                        f"COSMO_PACKAGE_DATA_DIR={selected_data_dir}\n"
                        f"COSMO_PACKAGE_APP_DATA_DIR={runtime_app_data_dir}\n"
                    ).encode()
                elif name in ("LICENSE", "share/licenses/cosmo-edge/LICENSE"):
                    data = b"Apache License\nVersion 2.0\nfixture\n"
                elif name in ("NOTICE", "share/licenses/cosmo-edge/NOTICE"):
                    data = b"CosmoEdge\nThird-party software\nfixture\n"
                elif name.endswith("ffmpeg/COPYING.LGPLv2.1"):
                    data = b"GNU LESSER GENERAL PUBLIC LICENSE\nfixture\n"
                elif name == "lib/libavcodec.so.58.134.100":
                    data = b"libavcodec license: LGPL version 2.1 or later\n"
                elif name.endswith("rknn-runtime/LICENSE"):
                    data = rknn_terms or (
                        b"RKNN SDK License\n"
                        b"1. License Grant\n"
                        b"redistribute its modifications or derivative works\n"
                        b"compatible with Products\n"
                    )
                elif rknn_guard and name == verifier.MODEL_GUARD_TERMS_FILE:
                    data = (b"explicitly authorized CosmoEdge fork\n"
                            b"not a general relicensing\n"
                            b"public distribution still requires a separate review\n")
                elif name == verifier.MODEL_GUARD_RKNN_RUNTIME_FILE:
                    data = guard_payload
                elif name == verifier.MODEL_GUARD_RKNN_HEADER_FILE:
                    data = header_payload
                elif name == verifier.MODEL_GUARD_RUNTIME_HASH_FILE:
                    data = (hashlib.sha256(guard_payload).hexdigest() + "  " +
                            verifier.MODEL_GUARD_RKNN_RUNTIME_FILE + "\n").encode()
                elif name == verifier.MODEL_GUARD_RKNN_SDK_MANIFEST_FILE:
                    data = json.dumps({
                        "release_id": "cmg-sdk-v2.4.0-rknn-abi1",
                        "certificate_storage": "caller-selected",
                        "source": {"repository": "cosmo-model-guard", "tree": "a" * 40},
                        "components": {
                            "include/cosmo_model_guard_rknn_v1.h": hashlib.sha256(header_payload).hexdigest(),
                            "lib/libcosmo_model_guard_rknn.so.1.0.0": hashlib.sha256(guard_payload).hexdigest(),
                            "bin/cosmo-model-provision": hashlib.sha256(provision_payload).hexdigest(),
                        },
                    }).encode()
                elif name == verifier.MODEL_GUARD_TERMS_FILE:
                    data = (
                        REPOSITORY / "prebuild/model-guard-v2/README.md"
                    ).read_bytes()
                elif name == verifier.MODEL_GUARD_RUNTIME_FILE:
                    data = (
                        model_guard_runtime
                        if model_guard_runtime is not None
                        else (
                            REPOSITORY
                            / "prebuild/model-guard-v2/lib/"
                            "libcosmo_model_guard.so.2.0.0"
                        ).read_bytes()
                    )
                elif name in verifier.REQUIRED_LICENSE_FILES:
                    data = b"fixture runtime license\n"
                else:
                    data = (
                        b"#!/bin/sh\n"
                        if name in executable_files or name.endswith("provision")
                        else b"V1.5.0\n"
                    )
                info = tarfile.TarInfo(f"{root}/{name}")
                info.size = len(data)
                info.mode = 0o755 if name in executable_files or name.endswith("provision") else 0o644
                archive.addfile(info, io.BytesIO(data))
            model_path = f"{root}/resource/models/preset/{model_file_name}"
            info = tarfile.TarInfo(model_path)
            info.size = len(model)
            info.mode = 0o644
            archive.addfile(info, io.BytesIO(model))
            config = json.dumps({"model_type": model_type}).encode()
            info = tarfile.TarInfo(f"{root}/resource/models/preset/config.json")
            info.size = len(config)
            info.mode = 0o644
            archive.addfile(info, io.BytesIO(config))
            if target_chip is not None:
                marker = f"{target_chip}\n".encode()
                info = tarfile.TarInfo(f"{root}/share/cosmo/target-chip.txt")
                info.size = len(marker)
                info.mode = 0o644
                archive.addfile(info, io.BytesIO(marker))
            if platform_chip is not None:
                platform_value: dict[str, object] = {
                    "chip": platform_chip,
                    "backend": "rknn",
                }
                if platform_runtime is not None:
                    platform_value["media"] = {
                        "runtime_profile": platform_runtime
                    }
                platform = json.dumps(platform_value).encode()
                info = tarfile.TarInfo(
                    f"{root}/share/cosmo/platform-profile.json"
                )
                info.size = len(platform)
                info.mode = 0o644
                archive.addfile(info, io.BytesIO(platform))
            if model_bundle_scope is not None:
                rknn = b"rv1126b-rknn-fixture"
                package_directory = "prod_RV1126B_9275710_YOLOV8_V1.0.0"
                package_model = f"{root}/resource/models/{package_directory}/model.rknn"
                info = tarfile.TarInfo(package_model)
                info.size = len(rknn)
                info.mode = 0o644
                archive.addfile(info, io.BytesIO(rknn))
                license_data = b"fixture model license\n"
                if include_bundle_license:
                    info = tarfile.TarInfo(
                        f"{root}/resource/licenses/model-assets/AGPL-3.0.txt"
                    )
                    info.size = len(license_data)
                    info.mode = 0o644
                    archive.addfile(info, io.BytesIO(license_data))
                bundle = json.dumps(
                    {
                        "schema_version": 1,
                        "chip": target_chip,
                        "usage_scope": model_bundle_scope,
                        "commercial_delivery": False,
                        "license": {
                            "spdx": "AGPL-3.0-only",
                            "path": "model-artifacts/LICENSES/AGPL-3.0.txt",
                            "sha256": hashlib.sha256(license_data).hexdigest(),
                        },
                        "models": [
                            {
                                "model": "yolov8",
                                "package_directory": package_directory,
                                "artifact": {
                                    "sha256": hashlib.sha256(rknn).hexdigest(),
                                    "size_bytes": len(rknn),
                                },
                            }
                        ],
                    }
                ).encode()
                info = tarfile.TarInfo(f"{root}/resource/model-bundle.json")
                info.size = len(bundle)
                info.mode = 0o644
                archive.addfile(info, io.BytesIO(bundle))
        digest = hashlib.md5(initial.read_bytes(), usedforsecurity=False).hexdigest()
        final = directory / f"{root}-{digest}.tar.gz"
        initial.rename(final)
        return final

    def test_open_accepts_plain_model(self) -> None:
        verifier.verify_package(self.make_package("public-runtime"), "public-runtime")

    def test_distribution_license_bundle_is_mandatory(self) -> None:
        for required in (
            "LICENSE",
            "NOTICE",
            "share/licenses/third-party/ffmpeg/COPYING.LGPLv2.1",
        ):
            with self.subTest(required=required):
                package = self.make_package(
                    "public-runtime", omit_required_file=required
                )
                with self.assertRaisesRegex(
                    verifier.PackageAuditError, "required file is missing"
                ):
                    verifier.verify_package(package, "public-runtime")

        rockchip_license = next(iter(verifier.RKNN_LICENSE_FILES))
        package = self.make_package(
            "public-runtime",
            target_chip="rv1126b",
            platform_chip="rv1126b",
            omit_required_file=rockchip_license,
        )
        with self.assertRaisesRegex(
            verifier.PackageAuditError, "runtime license is empty"
        ):
            verifier.verify_package(
                package, "public-runtime", target_chip="rv1126b"
            )

        model_guard = self.make_package(
            "public-runtime",
            target_chip="bm1688",
            include_model_guard=True,
            omit_required_file=verifier.MODEL_GUARD_TERMS_FILE,
        )
        with self.assertRaisesRegex(
            verifier.PackageAuditError, "runtime license is empty"
        ):
            verifier.verify_package(
                model_guard, "public-runtime", target_chip="bm1688"
            )

    def test_rockchip_rejects_incomplete_rknn_sdk_terms(self) -> None:
        package = self.make_package(
            "public-runtime",
            target_chip="rk3576",
            platform_chip="rk3576",
            platform_runtime="rk3576-mpp-rga",
            rknn_terms=b"Copyright Statement\nfixture\n",
        )
        with self.assertRaisesRegex(
            verifier.PackageAuditError, "RKNN runtime terms are invalid"
        ):
            verifier.verify_package(
                package, "public-runtime", target_chip="rk3576"
            )

    def test_model_guard_requires_approved_runtime_identity(self) -> None:
        approved = self.make_package(
            "public-runtime",
            target_chip="bm1688",
            include_model_guard=True,
        )
        verifier.verify_package(
            approved, "public-runtime", target_chip="bm1688"
        )

        unapproved = self.make_package(
            "public-runtime",
            target_chip="bm1688",
            include_model_guard=True,
            model_guard_runtime=b"unapproved-runtime\n",
        )
        with self.assertRaisesRegex(
            verifier.PackageAuditError, "not the approved artifact"
        ):
            verifier.verify_package(
                unapproved, "public-runtime", target_chip="bm1688"
            )

    def test_rv1126b_accepts_identified_community_example_bundle(self) -> None:
        package = self.make_package(
            "public-runtime",
            target_chip="rv1126b",
            platform_chip="rv1126b",
            model_bundle_scope="community-example",
        )
        verifier.verify_package(
            package,
            "public-runtime",
            target_chip="rv1126b",
        )

    def test_rv1126b_rejects_bundle_without_packaged_license(self) -> None:
        package = self.make_package(
            "public-runtime",
            target_chip="rv1126b",
            platform_chip="rv1126b",
            model_bundle_scope="community-example",
            include_bundle_license=False,
        )
        with self.assertRaisesRegex(
            verifier.PackageAuditError, "packaged model license is missing"
        ):
            verifier.verify_package(
                package,
                "public-runtime",
                target_chip="rv1126b",
            )

    def test_protected_accepts_encrypted_model(self) -> None:
        verifier.verify_package(
            self.make_package("production-release", b"CEMC" + b"encrypted"),
            "production-release",
        )

    def test_profile_checks_every_named_rknn_segment(self) -> None:
        protected = self.make_package(
            "production-release",
            b"CEMC" + b"encrypted",
            model_file_name="model0.rknn",
        )
        verifier.verify_package(protected, "production-release")

        for profile, payload in (
            ("production-release", b"plain"),
            ("public-runtime", b"CEMC" + b"encrypted"),
        ):
            with self.subTest(profile=profile):
                with self.assertRaises(verifier.PackageAuditError):
                    verifier.verify_package(
                        self.make_package(
                            profile,
                            payload,
                            model_file_name="model1.rknn",
                        ),
                        profile,
                    )

    def test_protected_rejects_plain_vllm_model(self) -> None:
        for model_type in ("qwen3vl", "qwen3_5"):
            with self.subTest(model_type=model_type):
                with self.assertRaises(verifier.PackageAuditError):
                    verifier.verify_package(
                        self.make_package("production-release", model_type=model_type),
                        "production-release",
                    )

    def test_channels_reject_each_others_model_format(self) -> None:
        with self.assertRaises(verifier.PackageAuditError):
            verifier.verify_package(
                self.make_package("public-runtime", b"CEMCencrypted"), "public-runtime"
            )
        with self.assertRaises(verifier.PackageAuditError):
            verifier.verify_package(
                self.make_package("production-release", b"plain"), "production-release"
            )

    def test_target_chip_and_rockchip_profile_are_bound_to_archive(self) -> None:
        with self.assertRaisesRegex(verifier.PackageAuditError, "marker is missing"):
            verifier.verify_package(
                self.make_package("public-runtime"), "public-runtime", "rv1126b"
            )

        with self.assertRaisesRegex(
            verifier.PackageAuditError, "platform profile is missing"
        ):
            verifier.verify_package(
                self.make_package("public-runtime", target_chip="rv1126b"),
                "public-runtime",
                "rv1126b",
            )

        package = self.make_package(
            "public-runtime", target_chip="rv1126b", platform_chip="rv1126b"
        )
        verifier.verify_package(package, "public-runtime", "rv1126b")
        verifier.verify_package(
            package,
            "public-runtime",
            "rv1126b",
            {
                "required_package_paths": ["share/cosmo/platform-profile.json"],
                "forbidden_package_paths": ["lib/librkllmrt.so"],
            },
        )

        with self.assertRaisesRegex(
            verifier.PackageAuditError, "required target package path is missing"
        ):
            verifier.verify_package(
                package,
                "public-runtime",
                "rv1126b",
                {"required_package_paths": ["share/licenses/missing"]},
            )
        with self.assertRaisesRegex(
            verifier.PackageAuditError, "forbidden target package path is present"
        ):
            verifier.verify_package(
                package,
                "public-runtime",
                "rv1126b",
                {"forbidden_package_paths": ["share/cosmo/target-chip.txt"]},
            )

        runtime_package = self.make_package(
            "public-runtime",
            target_chip="rv1126b",
            platform_chip="rv1126b",
            platform_runtime="runtime-a",
        )
        with self.assertRaisesRegex(
            verifier.PackageAuditError, "media runtime profile does not match"
        ):
            verifier.verify_package(
                runtime_package,
                "public-runtime",
                "rv1126b",
                {"media_runtime_profile": "runtime-b"},
            )

        with self.assertRaisesRegex(verifier.PackageAuditError, "target chip mismatch"):
            verifier.verify_package(package, "public-runtime", "rk3576")

        wrong_platform = self.make_package(
            "public-runtime", target_chip="rv1126b", platform_chip="rk3576"
        )
        with self.assertRaisesRegex(
            verifier.PackageAuditError, "platform profile does not match"
        ):
            verifier.verify_package(wrong_platform, "public-runtime", "rv1126b")

    def test_runtime_paths_match_target_chip(self) -> None:
        for target_chip in ("rk3576", "rv1126b"):
            package = self.make_package(
                "public-runtime",
                target_chip=target_chip,
                platform_chip=target_chip,
            )
            verifier.verify_package(package, "public-runtime", target_chip)

        sophon = self.make_package("public-runtime", target_chip="bm1688")
        verifier.verify_package(sophon, "public-runtime", "bm1688")

        wrong_data_root = self.make_package(
            "public-runtime",
            target_chip="rv1126b",
            platform_chip="rv1126b",
            runtime_data_dir="/data/cwaiuserdata",
        )
        with self.assertRaisesRegex(
            verifier.PackageAuditError, "runtime data directory does not match"
        ):
            verifier.verify_package(wrong_data_root, "public-runtime", "rv1126b")

        wrong_app_root = self.make_package(
            "public-runtime",
            target_chip="bm1688",
            runtime_app_data_dir="/userdata/cwai_data",
        )
        with self.assertRaisesRegex(
            verifier.PackageAuditError, "application directory is incompatible"
        ):
            verifier.verify_package(wrong_app_root, "public-runtime", "bm1688")

    def mutate_package(self, package, changes, reverse=False, additions=None):
        """Rewrite members and always recalculate MD5 so negatives reach the audit."""
        with tarfile.open(package, "r:gz") as source:
            members = [(m, source.extractfile(m).read() if m.isreg() else None)
                       for m in source]
        seen = set()
        rewritten = package.parent / "rewritten.tar.gz"
        with tarfile.open(rewritten, "w:gz") as target:
            for member, data in reversed(members) if reverse else members:
                name = member.name.split("/", 1)[-1]
                if name in changes:
                    seen.add(name)
                    change = changes[name]
                    if change is None:
                        continue
                    if isinstance(change, bytes):
                        data = change
                        member.size = len(data)
                    elif change == "directory":
                        member.type, member.size, data = tarfile.DIRTYPE, 0, None
                    elif change == "symlink":
                        member.type, member.size, data = tarfile.SYMTYPE, 0, None
                        member.linkname = "cosmo"
                    elif change == "not-executable":
                        member.mode = 0o644
                    else:
                        raise AssertionError(change)
                target.addfile(member, io.BytesIO(data) if data is not None else None)
            for name, data in (additions or {}).items():
                self.assertNotIn(name, {m.name.split("/", 1)[-1] for m, _ in members})
                member = tarfile.TarInfo("cosmo-V1.5.0/" + name)
                member.mode, member.size = 0o644, len(data)
                target.addfile(member, io.BytesIO(data))
        self.assertEqual(seen, set(changes), "mutation must affect every requested member")
        digest = hashlib.md5(rewritten.read_bytes(), usedforsecurity=False).hexdigest()
        final = package.parent / f"cosmo-V1.5.0-{digest}.tar.gz"
        rewritten.replace(final)
        return final

    def protected_fixture(self, chip):
        return self.make_package(
            "production-release", b"CEMCfixture", target_chip=chip,
            platform_chip=chip if chip == "rk3576" else None,
            platform_runtime="rk3576-mpp-rga" if chip == "rk3576" else None,
        )

    def test_explicit_protected_targets_and_member_order(self):
        for chip in ("bm1688", "rk3576"):
            with self.subTest(chip=chip):
                package = self.protected_fixture(chip)
                verifier.verify_package(package, "production-release", chip)
                verifier.verify_package(self.mutate_package(package, {}, reverse=True),
                                        "production-release", chip)

    def test_protected_requires_regular_matching_guard_and_executable_provisioner(self):
        for chip in ("bm1688", "cv186x", "rk3576", None, "unspecified"):
            guard = (verifier.MODEL_GUARD_RKNN_RUNTIME_FILE if chip == "rk3576"
                     else verifier.MODEL_GUARD_RUNTIME_FILE)
            for member in (guard, "bin/cosmo-model-provision"):
                for change in (None, "directory", "symlink"):
                    with self.subTest(chip=chip, member=member, change=change):
                        package = self.protected_fixture(chip)
                        verifier.verify_package(package, "production-release", chip)
                        negative = self.mutate_package(package, {member: change})
                        with self.assertRaisesRegex(verifier.PackageAuditError,
                                                    "Guard runtime|provision|SDK component"):
                            verifier.verify_package(negative, "production-release", chip)
            package = self.mutate_package(self.protected_fixture(chip),
                                         {"bin/cosmo-model-provision": "not-executable"})
            with self.assertRaisesRegex(verifier.PackageAuditError, "provision"):
                verifier.verify_package(package, "production-release", chip)

    def test_plaintext_and_missing_members_reach_intended_audit(self):
        for chip in ("bm1688", "rk3576"):
            package = self.protected_fixture(chip)
            for member in ("resource/models/preset/model.nn",):
                with self.subTest(chip=chip, member=member):
                    negative = self.mutate_package(package, {member: b"plain"})
                    with self.assertRaisesRegex(verifier.PackageAuditError, "plaintext"):
                        verifier.verify_package(negative, "production-release", chip)
            for member in verifier.REQUIRED_FILES | verifier.REQUIRED_EXECUTABLES:
                with self.subTest(chip=chip, member=member):
                    negative = self.mutate_package(package, {member: None})
                    with self.assertRaisesRegex(verifier.PackageAuditError, "required .*missing"):
                        verifier.verify_package(negative, "production-release", chip)

    def test_every_encrypted_segment_is_audited(self):
        for chip in ("bm1688", "rk3576"):
            names = ["resource/models/preset/model.nn", "resource/models/preset/model0.rknn",
                     "resource/models/preset/model1.rknn", "resource/models/other/model.nn"]
            package = self.mutate_package(self.protected_fixture(chip), {}, additions={
                name: b"CEMCencrypted segment" for name in names[1:]})
            verifier.verify_package(package, "production-release", chip)
            for name in names:
                with self.subTest(chip=chip, name=name):
                    negative = self.mutate_package(package, {name: b"plaintext segment"})
                    with self.assertRaisesRegex(verifier.PackageAuditError, "plaintext preset model: " + name):
                        verifier.verify_package(negative, "production-release", chip)

    def test_archive_md5_and_controlled_names_are_enforced(self):
        package = self.protected_fixture("bm1688")
        invalid_name = package.parent / ("cosmo-V1.5.0-" + "0" * 32 + ".tar.gz")
        shutil.copy2(package, invalid_name)
        with self.assertRaisesRegex(verifier.PackageAuditError, "MD5"):
            verifier.verify_package(invalid_name, "production-release", "bm1688")
        for filename in ("device-certificate.bin", "product-model-key-v1.bin",
                         "commissioning-ed25519.seed", "product-pepper-v1.bin"):
            negative = self.mutate_package(package, {}, additions={"resource/" + filename: b"opaque"})
            with self.assertRaisesRegex(verifier.PackageAuditError, "controlled secret"):
                verifier.verify_package(negative, "production-release", "bm1688")

    def test_rknn_manifest_formatting_and_identity(self):
        package = self.protected_fixture("rk3576")
        name = verifier.MODEL_GUARD_RKNN_SDK_MANIFEST_FILE
        with tarfile.open(package) as archive:
            manifest = json.load(archive.extractfile("cosmo-V1.5.0/" + name))
        equivalent = json.dumps(manifest, indent=4, sort_keys=True).encode()
        verifier.verify_package(self.mutate_package(package, {name: equivalent}),
                                "production-release", "rk3576")
        manifest["release_id"] = "incorrect-identity"
        with self.assertRaisesRegex(verifier.PackageAuditError, "manifest does not match"):
            verifier.verify_package(self.mutate_package(package, {name: json.dumps(manifest).encode()}),
                                    "production-release", "rk3576")

    def test_rv1126b_protected_remains_unsupported(self):
        package = self.make_package("production-release", b"CEMCfixture",
                                    target_chip="rv1126b", platform_chip="rv1126b")
        with self.assertRaisesRegex(verifier.PackageAuditError, "RV1126B packages are unsupported"):
            verifier.verify_package(package, "production-release", "rv1126b")

    def test_compatibility_target_does_not_count_as_explicit_chip(self):
        for chip in (None, "unspecified"):
            package = self.protected_fixture(chip)
            verifier.verify_package(package, "production-release")
            with self.assertRaisesRegex(verifier.PackageAuditError, "marker is missing|target chip mismatch"):
                verifier.verify_package(package, "production-release", "bm1688")

    def test_obsolete_release_payload_and_secrets_rejected(self):
        package = self.protected_fixture("bm1688")
        for name in (".release-bootstrap/old", "bin/release_updater", "resource/models/preset/device.cert"):
            with tarfile.open(package) as source:
                members = [(m, source.extractfile(m).read() if m.isreg() else None) for m in source]
            initial = package.parent / "extra.tar.gz"
            with tarfile.open(initial, "w:gz") as target:
                for member, data in members:
                    target.addfile(member, io.BytesIO(data) if data is not None else None)
                data = b"-----BEGIN PRIVATE KEY-----" if name.endswith("cert") else b"old"
                member = tarfile.TarInfo("cosmo-V1.5.0/" + name)
                member.size = len(data)
                target.addfile(member, io.BytesIO(data))
            digest = hashlib.md5(initial.read_bytes(), usedforsecurity=False).hexdigest()
            final = initial.parent / f"cosmo-V1.5.0-{digest}.tar.gz"
            initial.replace(final)
            with self.assertRaisesRegex(verifier.PackageAuditError, "obsolete|private key"):
                verifier.verify_package(final, "production-release", "bm1688")


if __name__ == "__main__":
    unittest.main()
