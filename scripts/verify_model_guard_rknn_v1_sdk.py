#!/usr/bin/env python3
"""Admit the production Model Guard RKNN ABI 1 SDK for the selected chip."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys
from typing import NoReturn


EXPECTED_EXPORTS = {
    "CmgRknnV1CloseArtifact@@CMGR_1.0",
    "CmgRknnV1GetArtifactInfo@@CMGR_1.0",
    "CmgRknnV1LoadSegment@@CMGR_1.0",
    "CmgRknnV1OpenArtifact@@CMGR_1.0",
}
REQUIRED_HEADER_LINES = {
    "#define CMG_RKNN_V1_ABI_MAJOR UINT32_C(1)",
    "#define CMG_RKNN_V1_SOURCE_RAW_RKNN UINT32_C(3)",
    "#define CMG_RKNN_V1_DEVICE_CERTIFICATE_SIZE UINT32_C(236)",
    "#define CMG_RKNN_V1_OPEN_OPTIONS_SIZE UINT32_C(32)",
    "#define CMG_RKNN_V1_ARTIFACT_INFO_SIZE UINT32_C(72)",
}
REQUIRED_FUNCTIONS = tuple(name.split("@@", 1)[0] for name in EXPECTED_EXPORTS)
FORBIDDEN_CERTIFICATE_DIRECTORIES = (
    b"/userdata/cwaiuserdata/model-guard",
    b"/data/cwaiuserdata/model-guard",
)
EXPECTED_BUILD_IMAGE = (
    "ghcr.io/cosmo-wander-ai/cosmo_edge-build-env_rockchip@"
    "sha256:0810c23042cbe86d3a1c91f848b9849a34d94222f3ad7b7418913a26da19e71b"
)


def fail(message: str) -> NoReturn:
    raise RuntimeError(message)


def checked_file(path: pathlib.Path, maximum_size: int) -> bytes:
    try:
        data = path.read_bytes()
    except OSError as error:
        fail(f"cannot read SDK file {path}: {error}")
    if not data or len(data) > maximum_size:
        fail(f"SDK file size is invalid: {path}")
    return data


def checked_symlink(path: pathlib.Path, target: str) -> None:
    if not path.is_symlink() or os.readlink(path) != target:
        fail(f"SDK linker alias is invalid: {path}")


def run_tool(tool: pathlib.Path, arguments: list[str]) -> str:
    if not tool.is_absolute() or not tool.is_file():
        fail(f"inspection tool is missing: {tool}")
    environment = dict(os.environ)
    environment["LC_ALL"] = "C"
    completed = subprocess.run(
        [str(tool), *arguments], check=False, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, env=environment
    )
    if completed.returncode != 0:
        fail(f"inspection tool failed: {tool.name}")
    return completed.stdout


def verify_stripped_elf(path: pathlib.Path, readelf: pathlib.Path) -> None:
    sections = run_tool(readelf, ["-S", "--wide", str(path)])
    names = set(re.findall(r"\[\s*\d+\]\s+(\.\S+)", sections))
    if ".symtab" in names or ".strtab" in names or any(
        name.startswith(".debug") for name in names
    ):
        fail(f"Model Guard SDK component is not stripped: {path.name}")


def verify_header(data: bytes) -> None:
    if len(data) > 128 * 1024 or b"\x00" in data:
        fail("Model Guard RKNN header size/content rejected")
    try:
        text = data.decode("utf-8", "strict")
    except UnicodeError as error:
        raise RuntimeError("Model Guard RKNN header is not UTF-8") from error
    if not REQUIRED_HEADER_LINES.issubset(set(text.splitlines())):
        fail("Model Guard RKNN header constants are incompatible")
    for function in REQUIRED_FUNCTIONS:
        if len(re.findall(rf"\b{re.escape(function)}\s*\(", text)) != 1:
            fail(f"Model Guard RKNN header declaration rejected: {function}")


def verify_library(library: pathlib.Path, readelf: pathlib.Path, nm: pathlib.Path) -> None:
    verify_stripped_elf(library, readelf)
    header = run_tool(readelf, ["-h", str(library)])
    if re.search(r"^\s*Type:\s+DYN\b", header, re.MULTILINE) is None:
        fail("Model Guard RKNN library is not a shared ELF image")
    dynamic = run_tool(readelf, ["-d", str(library)])
    runpaths = re.findall(r"\(RUNPATH\).*\[([^]]+)\]", dynamic)
    if runpaths != ["$ORIGIN"]:
        fail("Model Guard RKNN RUNPATH must be exactly $ORIGIN")
    needed = set(re.findall(r"\(NEEDED\).*\[([^]]+)\]", dynamic))
    if "librknnrt.so" not in needed or not any(name.startswith("libcrypto.so") for name in needed):
        fail("Model Guard RKNN runtime dependencies are incomplete")
    symbols = run_tool(nm, ["-D", "--defined-only", str(library)])
    exports = {
        fields[2] for line in symbols.splitlines()
        if len(fields := line.split()) == 3 and fields[1] in {"T", "W"}
    }
    if exports != EXPECTED_EXPORTS:
        fail(f"Model Guard RKNN exports are incompatible: {sorted(exports)}")


def verify_manifest(data: bytes, component_hashes: dict[str, str]) -> str:
    try:
        manifest = json.loads(data.decode("utf-8", "strict"))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise RuntimeError("Model Guard RKNN SDK manifest is invalid") from error
    if (
        not isinstance(manifest, dict)
        or manifest.get("schema_version") != 1
        or manifest.get("release_id") != "cmg-sdk-v2.4.0-rknn-abi1"
        or manifest.get("product_version") != "2.4.0"
        or manifest.get("components") != component_hashes
        or manifest.get("certificate_storage") != "caller-selected"
    ):
        fail("Model Guard RKNN SDK manifest contract mismatch")
    abi = manifest.get("abi")
    if (
        not isinstance(abi, dict)
        or abi.get("soname") != "libcosmo_model_guard_rknn.so.1"
        or abi.get("version_node") != "CMGR_1.0"
        or abi.get("major") != 1
        or set(abi.get("exports", [])) != set(REQUIRED_FUNCTIONS)
    ):
        fail("Model Guard RKNN SDK manifest ABI mismatch")
    expected_manifest_keys = {
        "schema_version",
        "release_id",
        "product_version",
        "abi",
        "source",
        "build",
        "components",
        "binding_contract",
        "certificate_storage",
    }
    if set(manifest) != expected_manifest_keys or manifest.get("binding_contract") != "internal-to-sdk":
        fail("Model Guard RKNN SDK manifest contains an unsupported contract")
    build = manifest.get("build")
    if not isinstance(build, dict) or build.get("image") != EXPECTED_BUILD_IMAGE:
        fail("Model Guard RKNN SDK manifest build image mismatch")
    source = manifest.get("source")
    tree = source.get("tree") if isinstance(source, dict) else None
    if (
        not isinstance(source, dict)
        or source.get("repository") != "cosmo-model-guard"
        or not isinstance(tree, str)
        or re.fullmatch(r"[0-9a-f]{40}", tree) is None
    ):
        fail("Model Guard RKNN SDK manifest source identity mismatch")
    return tree


def verify_rk3588_sdk(
    root: pathlib.Path, manifest: bytes, checksum_file: str, approved_checksum: str, components: dict[str, str]
) -> None:
    if not re.fullmatch(r"[0-9a-f]{64}", approved_checksum):
        fail("RK3588 requires an approved production SDK checksum-list SHA-256")
    checksums = pathlib.Path(checksum_file)
    if not checksums.is_absolute():
        fail("RK3588 requires an absolute SDK checksum-list path")
    for directory in {root, root.parent, root.parent.parent, checksums.parent}:
        if any(marker in directory.name or (directory / marker).exists()
               for marker in ("SYNTHETIC-TEST-ONLY", "TEST_FIXTURE_DO_NOT_DEPLOY")):
            fail("a marked Model Guard test fixture cannot satisfy production admission")
    contents = checked_file(checksums, 128 * 1024)
    if hashlib.sha256(contents).hexdigest() != approved_checksum:
        fail("RK3588 SDK checksum list does not match the approved SHA-256")
    expected = dict(components)
    for name in ("README.md", "SDK-MANIFEST.json"):
        expected[name] = hashlib.sha256(checked_file(root / name, 128 * 1024)).hexdigest()
    declared = {}
    for line in contents.decode("ascii").splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  (?:\./)?(.+)", line)
        if not match or match[2] in declared:
            fail("RK3588 SDK checksum list contains a malformed or duplicate entry")
        declared[match[2]] = match[1]
    if declared != expected:
        fail("RK3588 SDK components do not match the approved checksum list")
    document = json.loads(manifest.decode("utf-8"))
    if (
        not isinstance(document, dict)
        or document.get("release_id") != "cmg-sdk-v2.6.0-rknn-abi1"
        or document.get("guard_version") != [2, 6, 0]
        or document.get("minimum_glibc") != "2.31"
        or document.get("soname") != "libcosmo_model_guard_rknn.so.1"
        or document.get("symbol_version") != "CMGR_1.0"
        or document.get("platform_detection") != "device-tree-compatible"
        or document.get("targets") != {"rockchip-rk3576-aarch64": 2, "rockchip-rk3588-aarch64": 4}
        or document.get("binding_profiles") != {"rk3576-otp-id-v1": 2, "rk3588-otp-id-v1": 4}
    ):
        fail("RK3588 SDK manifest target or ABI mismatch")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--admission-profile", choices=("production-release",), required=True)
    parser.add_argument("--sdk-root", required=True)
    parser.add_argument("--target-chip", choices=("rk3576", "rk3588"), default="rk3576")
    parser.add_argument("--checksum-file", default="")
    parser.add_argument("--checksum-sha256", default="")
    parser.add_argument("--readelf", required=True)
    parser.add_argument("--nm", required=True)
    arguments = parser.parse_args()

    root = pathlib.Path(arguments.sdk_root)
    if not root.is_absolute() or not root.is_dir():
        fail("SDK root must be an existing absolute directory")
    header_path = root / "include/cosmo_model_guard_rknn_v1.h"
    library_version = "1.1.0" if arguments.target_chip == "rk3588" else "1.0.0"
    library_path = root / "lib" / f"libcosmo_model_guard_rknn.so.{library_version}"
    provision_path = root / "bin/cosmo-model-provision"
    manifest_path = root / "SDK-MANIFEST.json"
    header = checked_file(header_path, 128 * 1024)
    library = checked_file(library_path, 32 * 1024 * 1024)
    provision = checked_file(provision_path, 32 * 1024 * 1024)
    manifest = checked_file(manifest_path, 128 * 1024)
    if any(path in library or path in provision for path in FORBIDDEN_CERTIFICATE_DIRECTORIES):
        fail("Model Guard RKNN SDK embeds a fixed certificate directory")
    checked_symlink(root / "lib/libcosmo_model_guard_rknn.so.1", library_path.name)
    checked_symlink(root / "lib/libcosmo_model_guard_rknn.so", "libcosmo_model_guard_rknn.so.1")
    if (root / "share/cosmo-model-guard/TEST_FIXTURE_DO_NOT_DEPLOY").exists():
        fail("a marked Model Guard test fixture cannot satisfy production admission")
    verify_header(header)
    verify_library(library_path, pathlib.Path(arguments.readelf), pathlib.Path(arguments.nm))
    component_hashes = {
        "include/cosmo_model_guard_rknn_v1.h": hashlib.sha256(header).hexdigest(),
        library_path.relative_to(root).as_posix(): hashlib.sha256(library).hexdigest(),
        "bin/cosmo-model-provision": hashlib.sha256(provision).hexdigest(),
    }
    if arguments.target_chip == "rk3588":
        verify_rk3588_sdk(root, manifest, arguments.checksum_file, arguments.checksum_sha256, component_hashes)
        sdk_profile = "model-guard-v2.6.0-production-rk3588-rknn-abi1"
    else:
        source_tree = verify_manifest(manifest, component_hashes)
        print(f"source_tree={source_tree}")
        sdk_profile = "model-guard-v2.4.0-production-rk3576-rknn-abi1"
    verify_stripped_elf(provision_path, pathlib.Path(arguments.readelf))
    executable_header = run_tool(pathlib.Path(arguments.readelf), ["-h", str(provision_path)])
    if re.search(r"^\s*Type:\s+(?:DYN|EXEC)\b", executable_header, re.MULTILINE) is None:
        fail("cosmo-model-provision is not an ELF executable")

    print(f"admission_profile={arguments.admission_profile}")
    print(f"verified_sdk_root={root}")
    print(f"header_sha256={hashlib.sha256(header).hexdigest()}")
    print(f"library_sha256={hashlib.sha256(library).hexdigest()}")
    print(f"provision_tool_sha256={hashlib.sha256(provision).hexdigest()}")
    print(f"sdk_profile={sdk_profile}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"Model Guard RKNN ABI 1 SDK verification failed: {error}", file=sys.stderr)
        raise SystemExit(1)
