#!/usr/bin/python3
"""Audit permanent MD5 upgrade packages for the Open and Protected editions."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import stat
import tarfile


class PackageAuditError(RuntimeError):
    pass


PROFILES = ("public-runtime", "production-release")
TARGET_CHIPS = ("bm1688", "cv186x", "rk3576", "rv1126b", "unspecified")
REQUIRED_DIRS = {"bin", "files", "font", "lib", "resource", "scripts", "web"}
REQUIRED_EXECUTABLES = {
    "bin/cosmo-engine",
    "scripts/install.sh",
    "scripts/inte_run_start.sh",
    "scripts/run_start.sh",
    "scripts/start.sh",
    "scripts/stop.sh",
}
RUNTIME_PATHS_FILE = "share/cosmo/runtime-paths.env"
REQUIRED_LICENSE_FILES = {
    "LICENSE",
    "NOTICE",
    "share/licenses/cosmo-edge/LICENSE",
    "share/licenses/cosmo-edge/NOTICE",
    "share/licenses/third-party/cryptopp/License.txt",
    "share/licenses/third-party/cryptopp-cmake/LICENSE",
    "share/licenses/third-party/curl/COPYING",
    "share/licenses/third-party/eigen/LICENSE.MPL2.0",
    "share/licenses/third-party/ffmpeg/COPYING.LGPLv2.1",
    "share/licenses/third-party/ffmpeg/LICENSE.md",
    "share/licenses/third-party/fmt/LICENSE.rst",
    "share/licenses/third-party/glog/COPYING",
    "share/licenses/third-party/libevent/LICENSE",
    "share/licenses/third-party/libsophon/LICENSE",
    "share/licenses/third-party/libuuid/COPYING",
    "share/licenses/third-party/mp4v2/COPYING",
    "share/licenses/third-party/onnxruntime/LICENSE",
    "share/licenses/third-party/onnxruntime/ThirdPartyNotices.txt",
    "share/licenses/third-party/openssl/LICENSE.txt",
    "share/licenses/third-party/paho-mqtt-c/LICENSE",
    "share/licenses/third-party/paho-mqtt-c/NOTICE",
    "share/licenses/third-party/srs/LICENSE",
    "share/licenses/third-party/sqlitecpp/LICENSE.txt",
    "share/licenses/third-party/tokenizers-cpp/LICENSE",
    "share/licenses/third-party/usockets/LICENSE",
    "share/licenses/third-party/uwebsockets/LICENSE",
    "share/licenses/third-party/zlib/LICENSE",
}
RKNN_LICENSE_FILES = {
    "share/licenses/third-party/rknn-runtime/LICENSE",
    "share/licenses/third-party/rockchip-mpp/Apache-2.0",
    "share/licenses/third-party/rockchip-mpp/MIT",
    "share/licenses/third-party/rockchip-librga/COPYING",
}
MODEL_GUARD_TERMS_FILE = "share/licenses/cosmo-model-guard/ARTIFACT-TERMS.md"
MODEL_GUARD_RUNTIME_FILE = "lib/libcosmo_model_guard.so.2.0.0"
MODEL_GUARD_RKNN_RUNTIME_FILE = "lib/libcosmo_model_guard_rknn.so.1.0.0"
MODEL_GUARD_RUNTIME_HASH_FILE = "share/cosmo-model-guard/runtime.sha256"
MODEL_GUARD_RKNN_SDK_MANIFEST_FILE = "share/cosmo-model-guard/SDK-MANIFEST.json"
MODEL_GUARD_RKNN_HEADER_FILE = "share/cosmo-model-guard/cosmo_model_guard_rknn_v1.h"
APPROVED_MODEL_GUARD_RUNTIME_SHA256 = (
    "74ff8b456548e615882e5c9ee6dd18a51a2caf8124d761d7243dad014310042c"
)
REQUIRED_FILES = {
    "bin/version.txt",
    "scripts/common.sh",
    RUNTIME_PATHS_FILE,
} | REQUIRED_LICENSE_FILES
RUNTIME_DATA_DIRS = {
    "default": "/data/cwaiuserdata",
    "rockchip": "/userdata/cwaiuserdata",
}
RUNTIME_APP_DATA_DIR = "/appfs/cosmo_wander/cwai_data"
PRIVATE_MARKERS = (
    b"-----BEGIN PRIVATE KEY-----",
    b"-----BEGIN ENCRYPTED PRIVATE KEY-----",
    b"-----BEGIN RSA PRIVATE KEY-----",
    b"-----BEGIN EC PRIVATE KEY-----",
    b"-----BEGIN OPENSSH PRIVATE KEY-----",
)
FORBIDDEN_BASENAMES = {
    "commissioning-ed25519.seed",
    "device-certificate.bin",
    "product-model-key-v1.bin",
    "product-pepper-v1.bin",
    "release-private-key.o",
}


def archive_md5(path: pathlib.Path) -> str:
    digest = hashlib.md5(usedforsecurity=False)
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def content_sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def verify_archive_name(path: pathlib.Path) -> None:
    match = re.fullmatch(
        r"cosmo-[Vv]\d+\.\d+\.\d+-([0-9a-fA-F]{32})\.tar\.gz", path.name
    )
    if match is None or match.group(1).lower() != archive_md5(path):
        raise PackageAuditError("archive name must contain its exact MD5 digest")


def parse_runtime_paths(data: bytes) -> dict[str, str]:
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as error:
        raise PackageAuditError("runtime path declaration is not UTF-8") from error

    expected_keys = {
        "COSMO_PACKAGE_DATA_DIR",
        "COSMO_PACKAGE_APP_DATA_DIR",
    }
    values: dict[str, str] = {}
    for line in text.splitlines():
        if not line:
            continue
        if "=" not in line:
            raise PackageAuditError("runtime path declaration contains an invalid line")
        key, value = line.split("=", 1)
        if key not in expected_keys:
            raise PackageAuditError(
                f"runtime path declaration contains an unsupported key: {key}"
            )
        if key in values:
            raise PackageAuditError(
                f"runtime path declaration contains a duplicate key: {key}"
            )
        if not value.startswith("/") or any(character.isspace() for character in value):
            raise PackageAuditError(
                f"runtime path declaration contains an invalid path: {key}"
            )
        values[key] = value

    missing = expected_keys.difference(values)
    if missing:
        raise PackageAuditError(
            f"runtime path declaration is missing: {', '.join(sorted(missing))}"
        )
    return values


def verify_runtime_license_bundle(
    contents: dict[str, bytes], target_chip: str | None, profile: str
) -> None:
    required = set(REQUIRED_LICENSE_FILES)
    if target_chip in ("rk3576", "rv1126b"):
        required.update(RKNN_LICENSE_FILES)
    if MODEL_GUARD_RUNTIME_FILE in contents or MODEL_GUARD_RKNN_RUNTIME_FILE in contents:
        required.add(MODEL_GUARD_TERMS_FILE)

    for filename in required:
        if not contents.get(filename, b"").strip():
            raise PackageAuditError(f"runtime license is empty: {filename}")

    if b"Apache License\nVersion 2.0" not in contents["LICENSE"]:
        raise PackageAuditError("package root LICENSE is not Apache-2.0")
    if (
        b"CosmoEdge" not in contents["NOTICE"]
        or b"Third-party software" not in contents["NOTICE"]
    ):
        raise PackageAuditError("package root NOTICE is incomplete")
    if (
        b"GNU LESSER GENERAL PUBLIC LICENSE"
        not in contents["share/licenses/third-party/ffmpeg/COPYING.LGPLv2.1"]
    ):
        raise PackageAuditError("packaged FFmpeg LGPL text is invalid")
    if contents["LICENSE"] != contents["share/licenses/cosmo-edge/LICENSE"]:
        raise PackageAuditError("package root and shared LICENSE copies differ")
    if contents["NOTICE"] != contents["share/licenses/cosmo-edge/NOTICE"]:
        raise PackageAuditError("package root and shared NOTICE copies differ")

    ffmpeg_license_identity = b"libavcodec license: LGPL version 2.1 or later"
    ffmpeg_libraries = [
        data
        for name, data in contents.items()
        if name.startswith("lib/libavcodec.so.")
    ]
    if not ffmpeg_libraries or not any(
        ffmpeg_license_identity in data for data in ffmpeg_libraries
    ):
        raise PackageAuditError(
            "packaged FFmpeg runtime is not identified as LGPL-2.1-or-later"
        )
    if target_chip in ("rk3576", "rv1126b"):
        rknn_terms = contents[
            "share/licenses/third-party/rknn-runtime/LICENSE"
        ]
        required_rknn_terms = (
            b"RKNN SDK License",
            b"1. License Grant",
            b"redistribute its modifications or derivative works",
            b"compatible with Products",
        )
        if any(marker not in rknn_terms for marker in required_rknn_terms):
            raise PackageAuditError("packaged RKNN runtime terms are invalid")
    if MODEL_GUARD_RUNTIME_FILE in contents:
        model_guard_terms = contents[MODEL_GUARD_TERMS_FILE]
        required_model_guard_terms = (
            b"does not grant or alter artifact licensing or redistribution rights",
            b"cosmo-wander-ai/cosmo-edge/issues/59",
            b"cosmo-wander-ai/cosmo-edge/pull/101",
            APPROVED_MODEL_GUARD_RUNTIME_SHA256.encode("ascii"),
        )
        if any(
            marker not in model_guard_terms
            for marker in required_model_guard_terms
        ):
            raise PackageAuditError(
                "packaged Model Guard artifact terms are invalid"
            )
        if (
            content_sha256(contents[MODEL_GUARD_RUNTIME_FILE])
            != APPROVED_MODEL_GUARD_RUNTIME_SHA256
        ):
            raise PackageAuditError(
                "packaged Model Guard runtime is not the approved artifact"
            )
    if MODEL_GUARD_RKNN_RUNTIME_FILE in contents:
        if target_chip != "rk3576" or profile != "production-release":
            raise PackageAuditError(
                "RKNN Model Guard is valid only in a protected RK3576 package"
            )
        terms = b" ".join(contents[MODEL_GUARD_TERMS_FILE].lower().split())
        for marker in (
            b"explicitly authorized cosmoedge fork",
            b"not a general relicensing",
            b"public distribution still requires a separate review",
        ):
            if marker not in terms:
                raise PackageAuditError("RKNN Model Guard candidate terms are incomplete")
        declaration = contents.get(MODEL_GUARD_RUNTIME_HASH_FILE)
        expected = (
            content_sha256(contents[MODEL_GUARD_RKNN_RUNTIME_FILE]).encode("ascii")
            + b"  " + MODEL_GUARD_RKNN_RUNTIME_FILE.encode("ascii") + b"\n"
        )
        if declaration != expected:
            raise PackageAuditError("RKNN Model Guard runtime hash declaration mismatch")
        try:
            sdk_manifest = json.loads(
                contents[MODEL_GUARD_RKNN_SDK_MANIFEST_FILE].decode("utf-8")
            )
        except (KeyError, UnicodeDecodeError, json.JSONDecodeError) as error:
            raise PackageAuditError("RKNN Model Guard SDK manifest is missing or invalid") from error
        required_sdk_components = (
            MODEL_GUARD_RKNN_HEADER_FILE,
            MODEL_GUARD_RKNN_RUNTIME_FILE,
            "bin/cosmo-model-provision",
        )
        if any(name not in contents for name in required_sdk_components):
            raise PackageAuditError("RKNN Model Guard SDK component is missing")
        expected_components = {
            "include/cosmo_model_guard_rknn_v1.h": content_sha256(
                contents[MODEL_GUARD_RKNN_HEADER_FILE]
            ),
            "lib/libcosmo_model_guard_rknn.so.1.0.0": content_sha256(
                contents[MODEL_GUARD_RKNN_RUNTIME_FILE]
            ),
            "bin/cosmo-model-provision": content_sha256(
                contents["bin/cosmo-model-provision"]
            ),
        }
        source = sdk_manifest.get("source", {})
        if (
            sdk_manifest.get("release_id") != "cmg-sdk-v2.4.0-rknn-abi1"
            or sdk_manifest.get("components") != expected_components
            or sdk_manifest.get("certificate_storage") != "caller-selected"
            or source.get("repository") != "cosmo-model-guard"
            or re.fullmatch(r"[0-9a-f]{40}", str(source.get("tree", ""))) is None
        ):
            raise PackageAuditError("RKNN Model Guard SDK manifest does not match package contents")


def verify_model_bundle(
    entries: dict[str, tarfile.TarInfo],
    contents: dict[str, bytes],
    target_chip: str | None,
) -> None:
    bundle_name = "resource/model-bundle.json"
    package_rknn_models = {
        name: data
        for name, data in contents.items()
        if name.startswith("resource/models/") and name.endswith(".rknn")
    }
    if bundle_name not in contents:
        if target_chip == "rv1126b" and package_rknn_models:
            raise PackageAuditError(
                "RV1126B models require a packaged model-bundle manifest"
            )
        return

    try:
        bundle = json.loads(contents[bundle_name].decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise PackageAuditError("model bundle is not valid UTF-8 JSON") from error
    if not isinstance(bundle, dict) or bundle.get("schema_version") != 1:
        raise PackageAuditError("model bundle schema is unsupported")
    bundle_chip = bundle.get("chip")
    if target_chip is not None and bundle_chip != target_chip:
        raise PackageAuditError("model bundle chip does not match the package")
    usage_scope = bundle.get("usage_scope")
    if usage_scope not in ("community-example", "external"):
        raise PackageAuditError("model bundle usage scope is unsupported")
    if usage_scope == "community-example" and bundle.get("commercial_delivery") is not False:
        raise PackageAuditError(
            "community example model bundle must reject commercial delivery"
        )

    license_record = bundle.get("license")
    if not isinstance(license_record, dict):
        raise PackageAuditError("model bundle license record is missing")
    license_source = license_record.get("path")
    license_sha = license_record.get("sha256")
    license_spdx = license_record.get("spdx")
    if not all(
        isinstance(value, str) and value
        for value in (license_source, license_sha, license_spdx)
    ):
        raise PackageAuditError("model bundle license record is incomplete")
    license_basename = pathlib.PurePosixPath(license_source).name
    packaged_license = f"resource/licenses/model-assets/{license_basename}"
    if packaged_license not in contents:
        raise PackageAuditError("packaged model license is missing")
    if content_sha256(contents[packaged_license]) != license_sha:
        raise PackageAuditError("packaged model license hash mismatch")

    model_records = bundle.get("models")
    if not isinstance(model_records, list) or not model_records:
        raise PackageAuditError("model bundle has no model records")
    expected_models: set[str] = set()
    seen_names: set[str] = set()
    for index, record in enumerate(model_records):
        if not isinstance(record, dict):
            raise PackageAuditError(f"model bundle record {index} is invalid")
        model_name = record.get("model")
        package_directory = record.get("package_directory")
        artifact = record.get("artifact")
        if (
            not isinstance(model_name, str)
            or not model_name
            or model_name in seen_names
            or not isinstance(package_directory, str)
            or not re.fullmatch(r"[A-Za-z0-9._-]+", package_directory)
            or not isinstance(artifact, dict)
        ):
            raise PackageAuditError(f"model bundle record {index} is incomplete")
        seen_names.add(model_name)
        package_model = f"resource/models/{package_directory}/model.rknn"
        expected_models.add(package_model)
        if package_model not in contents:
            raise PackageAuditError(f"packaged model is missing: {model_name}")
        if artifact.get("size_bytes") != len(contents[package_model]):
            raise PackageAuditError(f"packaged model size mismatch: {model_name}")
        if artifact.get("sha256") != content_sha256(contents[package_model]):
            raise PackageAuditError(f"packaged model hash mismatch: {model_name}")
    if expected_models != set(package_rknn_models):
        raise PackageAuditError("packaged RKNN model inventory differs from its bundle")


def verify_package(
    path: pathlib.Path,
    profile: str,
    target_chip: str | None = None,
    target_policy: dict[str, object] | None = None,
) -> None:
    if not path.is_absolute() or path.is_symlink() or not path.is_file():
        raise PackageAuditError("archive must be one absolute regular file")
    verify_archive_name(path)
    entries: dict[str, tarfile.TarInfo] = {}
    contents: dict[str, bytes] = {}
    root: str | None = None
    with tarfile.open(path, "r:gz") as archive:
        for member in archive:
            parts = pathlib.PurePosixPath(member.name).parts
            if not parts or any(part in ("", ".", "..") for part in parts):
                raise PackageAuditError(f"unsafe archive member: {member.name}")
            root = parts[0] if root is None else root
            if parts[0] != root:
                raise PackageAuditError("archive must contain exactly one package root")
            relative = pathlib.PurePosixPath(*parts[1:]).as_posix()
            if not relative or relative == ".":
                continue
            if relative in entries or not (member.isdir() or member.isreg() or member.issym()):
                raise PackageAuditError(f"unsupported or duplicate member: {relative}")
            entries[relative] = member
            if member.isreg():
                stream = archive.extractfile(member)
                if stream is None:
                    raise PackageAuditError(f"cannot read member: {relative}")
                data = stream.read()
                contents[relative] = data
                if any(marker in data for marker in PRIVATE_MARKERS):
                    raise PackageAuditError(f"private key material is forbidden: {relative}")

    for directory in REQUIRED_DIRS:
        if directory not in entries or not entries[directory].isdir():
            raise PackageAuditError(f"required directory is missing: {directory}")
    for filename in REQUIRED_FILES:
        if filename not in entries or not entries[filename].isreg():
            raise PackageAuditError(f"required file is missing: {filename}")
    for filename in REQUIRED_EXECUTABLES:
        entry = entries.get(filename)
        if entry is None or not entry.isreg() or not (entry.mode & stat.S_IXUSR):
            raise PackageAuditError(f"required executable is missing: {filename}")

    runtime_paths = parse_runtime_paths(contents[RUNTIME_PATHS_FILE])
    if runtime_paths["COSMO_PACKAGE_APP_DATA_DIR"] != RUNTIME_APP_DATA_DIR:
        raise PackageAuditError(
            "runtime application directory is incompatible with the installation layout"
        )

    for filename in entries:
        if pathlib.PurePosixPath(filename).name.lower() in FORBIDDEN_BASENAMES:
            raise PackageAuditError(f"controlled secret is forbidden: {filename}")
        if filename.startswith(".release-bootstrap/") or "release_updater" in filename:
            raise PackageAuditError(f"obsolete signed-release material is forbidden: {filename}")

    if target_policy is not None:
        for field in ("required_package_paths", "forbidden_package_paths"):
            values = target_policy.get(field, [])
            if not isinstance(values, list) or not all(
                isinstance(value, str) and value for value in values
            ):
                raise PackageAuditError(f"target package policy {field} must be a string list")
        for required in target_policy.get("required_package_paths", []):
            if required not in entries:
                raise PackageAuditError(
                    f"required target package path is missing: {required}"
                )
        for forbidden in target_policy.get("forbidden_package_paths", []):
            if forbidden in entries:
                raise PackageAuditError(
                    f"forbidden target package path is present: {forbidden}"
                )

    marker_name = "share/cosmo/target-chip.txt"
    actual_chip: str | None = None
    if marker_name in entries:
        marker = entries.get(marker_name)
        if marker is None or not marker.isreg():
            raise PackageAuditError(f"target chip marker is invalid: {marker_name}")
        try:
            actual_chip = contents[marker_name].decode("utf-8").strip().lower()
        except UnicodeDecodeError as error:
            raise PackageAuditError("target chip marker is not UTF-8") from error
        if actual_chip not in TARGET_CHIPS:
            raise PackageAuditError(f"unsupported target chip marker: {actual_chip}")

    if target_chip is not None:
        if actual_chip is None:
            raise PackageAuditError(f"target chip marker is missing: {marker_name}")
        if actual_chip != target_chip:
            raise PackageAuditError(
                f"target chip mismatch: expected {target_chip}, package contains {actual_chip}"
            )

    effective_chip = target_chip or actual_chip
    if effective_chip is not None:
        expected_data_dir = (
            RUNTIME_DATA_DIRS["rockchip"]
            if effective_chip in ("rk3576", "rv1126b")
            else RUNTIME_DATA_DIRS["default"]
        )
        if runtime_paths["COSMO_PACKAGE_DATA_DIR"] != expected_data_dir:
            raise PackageAuditError(
                "runtime data directory does not match target chip: "
                f"expected {expected_data_dir} for {effective_chip}"
            )
    elif runtime_paths["COSMO_PACKAGE_DATA_DIR"] not in RUNTIME_DATA_DIRS.values():
        raise PackageAuditError("runtime data directory is unsupported")

    verify_runtime_license_bundle(contents, effective_chip, profile)

    verify_model_bundle(entries, contents, effective_chip)

    if target_chip is not None:
        if target_chip in ("rk3576", "rv1126b"):
            platform_name = "share/cosmo/platform-profile.json"
            platform_entry = entries.get(platform_name)
            if platform_entry is None or not platform_entry.isreg():
                raise PackageAuditError(
                    f"Rockchip platform profile is missing: {platform_name}"
                )
            try:
                platform = json.loads(contents[platform_name].decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError) as error:
                raise PackageAuditError(
                    "Rockchip platform profile is not valid UTF-8 JSON"
                ) from error
            if not isinstance(platform, dict) or platform.get("chip") != target_chip:
                raise PackageAuditError(
                    f"Rockchip platform profile does not match {target_chip}"
                )
            if platform.get("backend") != "rknn":
                raise PackageAuditError("Rockchip platform profile must use RKNN")
            if target_policy is not None:
                expected_runtime = target_policy.get("media_runtime_profile")
                platform_media = platform.get("media")
                actual_runtime = (
                    platform_media.get("runtime_profile")
                    if isinstance(platform_media, dict)
                    else None
                )
                if expected_runtime and actual_runtime != expected_runtime:
                    raise PackageAuditError(
                        "Rockchip media runtime profile does not match target policy"
                    )

                manifest_name = "share/cosmo/platform/rockchip-media-manifest.json"
                if manifest_name in contents:
                    try:
                        manifest = json.loads(contents[manifest_name].decode("utf-8"))
                    except (UnicodeDecodeError, json.JSONDecodeError) as error:
                        raise PackageAuditError(
                            "Rockchip media manifest is not valid UTF-8 JSON"
                        ) from error
                    if (
                        not isinstance(manifest, dict)
                        or manifest.get("runtime_profile") != expected_runtime
                    ):
                        raise PackageAuditError(
                            "Rockchip media manifest does not match target policy"
                        )

    provision = entries.get("bin/cosmo-model-provision")
    if profile == "public-runtime" and provision is not None:
        raise PackageAuditError("Open package must not contain the provisioning tool")
    if profile == "production-release" and (
        provision is None or not provision.isreg() or not (provision.mode & stat.S_IXUSR)
    ):
        raise PackageAuditError("Protected package requires cosmo-model-provision")
    if profile == "public-runtime" and MODEL_GUARD_RKNN_RUNTIME_FILE in contents:
        raise PackageAuditError("Open RK3576 package must not contain Model Guard")
    if (
        profile == "production-release"
        and effective_chip == "rk3576"
        and MODEL_GUARD_RKNN_RUNTIME_FILE not in contents
    ):
        raise PackageAuditError("Protected RK3576 package requires RKNN Model Guard")

    models = {
        name: data
        for name, data in contents.items()
        if name.startswith("resource/models/")
        and (name.endswith("/model.nn") or name.endswith(".rknn"))
    }
    for name, data in models.items():
        encrypted = data.startswith(b"CEMC")
        if profile == "public-runtime" and encrypted:
            raise PackageAuditError(f"Open package contains an encrypted preset model: {name}")
        if profile == "production-release" and not encrypted:
            raise PackageAuditError(f"Protected package contains a plaintext preset model: {name}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", required=True)
    parser.add_argument("--build-profile", required=True, choices=PROFILES)
    parser.add_argument("--target-chip", choices=TARGET_CHIPS)
    parser.add_argument("--target-policy-lock", type=pathlib.Path)
    arguments = parser.parse_args()
    try:
        target_policy = None
        if arguments.target_policy_lock is not None:
            if arguments.target_chip is None:
                raise PackageAuditError(
                    "--target-policy-lock requires --target-chip"
                )
            lock = json.loads(
                arguments.target_policy_lock.read_text(encoding="utf-8")
            )
            target_policy = lock["targets"][arguments.target_chip]
            if not isinstance(target_policy, dict):
                raise PackageAuditError("selected target package policy must be an object")
        verify_package(
            pathlib.Path(arguments.archive),
            arguments.build_profile,
            arguments.target_chip,
            target_policy,
        )
    except (
        OSError,
        KeyError,
        TypeError,
        UnicodeError,
        json.JSONDecodeError,
        tarfile.TarError,
        PackageAuditError,
    ) as error:
        parser.error(str(error))
    print(f"Verified {arguments.build_profile} MD5 upgrade package: {arguments.archive}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
