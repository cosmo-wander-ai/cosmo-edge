#!/usr/bin/env python3
"""Stage target-specific RKNN resources from shared configs and explicit artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


PROJECT_ROOT = Path(__file__).resolve().parents[2]
MANIFEST_SCHEMA_VERSION = 2
ARTIFACT_MANIFEST_SCHEMA_VERSION = 1


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def repository_path(raw: str | Path, field: str) -> Path:
    path = (PROJECT_ROOT / raw).resolve()
    try:
        path.relative_to(PROJECT_ROOT)
    except ValueError as error:
        raise ValueError(f"{field} must stay in the repository") from error
    return path


def parse_artifact(value: str) -> tuple[str, Path]:
    name, separator, raw_path = value.partition("=")
    if not separator or not name or not raw_path:
        raise argparse.ArgumentTypeError("artifact must be MODEL_NAME=PATH")
    path = Path(raw_path).expanduser().resolve()
    if not path.is_file():
        raise argparse.ArgumentTypeError(f"artifact does not exist: {path}")
    if path.suffix.lower() != ".rknn":
        raise argparse.ArgumentTypeError(f"artifact must be an .rknn file: {path}")
    return name, path


def replace_platform_tokens(value: Any, source_token: str, target_token: str) -> Any:
    if isinstance(value, str):
        return value.replace(source_token, target_token).replace(
            source_token.lower(), target_token.lower()
        )
    if isinstance(value, list):
        return [replace_platform_tokens(item, source_token, target_token) for item in value]
    if isinstance(value, dict):
        return {
            key: replace_platform_tokens(item, source_token, target_token)
            for key, item in value.items()
        }
    return value


def model_templates(template_root: Path) -> dict[str, tuple[Path, dict[str, Any]]]:
    templates: dict[str, tuple[Path, dict[str, Any]]] = {}
    for config_path in sorted((template_root / "models").glob("*/config.json")):
        config = json.loads(config_path.read_text(encoding="utf-8"))
        code = str(config.get("algorithm_code", ""))
        if not code:
            raise ValueError(f"template model config has no algorithm_code: {config_path}")
        if code in templates:
            raise ValueError(f"duplicate template algorithm_code: {code}")
        templates[code] = (config_path.parent, config)
    if not templates:
        raise ValueError(f"template resource has no model configs: {template_root}")
    return templates


def algorithm_atomic_codes(document: dict[str, Any]) -> set[str]:
    raw = document.get("atomicList", "[]")
    try:
        entries = json.loads(raw) if isinstance(raw, str) else raw
    except json.JSONDecodeError as error:
        raise ValueError("template algorithm atomicList is invalid JSON") from error
    if not isinstance(entries, list):
        raise ValueError("template algorithm atomicList must be an array")
    return {
        str(entry.get("atomicCode"))
        for entry in entries
        if isinstance(entry, dict) and entry.get("atomicCode") is not None
    }


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def platform_context(
    raw_profile_path: str | Path, raw_output_dir: str | Path | None = None
) -> tuple[Path, dict[str, Any], Path, Path, str]:
    profile_path = repository_path(raw_profile_path, "--platform-profile")
    if not profile_path.is_file():
        raise ValueError(f"platform profile does not exist: {profile_path}")
    profile = json.loads(profile_path.read_text(encoding="utf-8"))
    if profile.get("backend") != "rknn":
        raise ValueError("platform profile backend must be rknn")

    packaging = profile.get("packaging", {})
    target_token = str(packaging.get("directory_token", ""))
    if not re.fullmatch(r"[A-Z0-9]+", target_token):
        raise ValueError("platform profile has no packaging.directory_token")
    template_root = repository_path(
        str(packaging.get("resource_template_directory", "")),
        "packaging.resource_template_directory",
    )
    default_output = str(packaging.get("resource_overlay_directory", ""))
    output_dir = repository_path(raw_output_dir or default_output, "--output-dir")
    return profile_path, profile, template_root, output_dir, target_token


def manifest_path(root: Path, document: dict[str, Any], field: str) -> Path:
    raw_path = document.get("path")
    if not isinstance(raw_path, str) or not raw_path:
        raise ValueError(f"resource manifest has no {field}.path")
    path = (root / raw_path).resolve()
    try:
        path.relative_to(root.resolve())
    except ValueError as error:
        raise ValueError(f"resource manifest {field}.path escapes its root") from error
    return path


def verify_file_record(root: Path, document: dict[str, Any], field: str) -> Path:
    path = manifest_path(root, document, field)
    if not path.is_file():
        raise ValueError(f"resource manifest {field} is missing: {path}")
    expected_sha = document.get("sha256")
    if not isinstance(expected_sha, str) or sha256(path) != expected_sha:
        raise ValueError(f"resource manifest {field} hash mismatch: {path}")
    return path


def load_artifact_manifest(
    raw_manifest_path: str | Path, expected_chip: str | None = None
) -> tuple[Path, dict[str, Any], list[tuple[str, Path]], Path]:
    manifest_file = repository_path(raw_manifest_path, "--artifact-manifest")
    if not manifest_file.is_file():
        raise ValueError(f"artifact manifest does not exist: {manifest_file}")
    document = json.loads(manifest_file.read_text(encoding="utf-8"))
    if document.get("schema_version") != ARTIFACT_MANIFEST_SCHEMA_VERSION:
        raise ValueError(
            "artifact manifest schema is stale: "
            f"expected {ARTIFACT_MANIFEST_SCHEMA_VERSION}, "
            f"got {document.get('schema_version')}"
        )
    chip = document.get("chip")
    if not isinstance(chip, str) or not chip:
        raise ValueError("artifact manifest has no chip")
    if expected_chip is not None and chip != expected_chip:
        raise ValueError(
            f"artifact manifest chip does not match the platform profile: {chip}"
        )
    usage_scope = document.get("usage_scope")
    if usage_scope not in {"community-example", "external"}:
        raise ValueError("artifact manifest has an unsupported usage_scope")

    license_record = document.get("license")
    if not isinstance(license_record, dict):
        raise ValueError("artifact manifest has no license record")
    license_path = verify_file_record(
        PROJECT_ROOT, license_record, "artifact_manifest.license"
    )
    spdx = license_record.get("spdx")
    if not isinstance(spdx, str) or not spdx:
        raise ValueError("artifact manifest license has no SPDX identifier")

    model_records = document.get("models")
    if not isinstance(model_records, list) or not model_records:
        raise ValueError("artifact manifest has no model records")
    artifacts: list[tuple[str, Path]] = []
    seen_models: set[str] = set()
    for index, record in enumerate(model_records):
        field = f"artifact_manifest.models[{index}]"
        if not isinstance(record, dict):
            raise ValueError(f"{field} must be an object")
        model_name = record.get("model")
        if (
            not isinstance(model_name, str)
            or not model_name
            or model_name in seen_models
        ):
            raise ValueError(f"{field} has an invalid or duplicate model")
        seen_models.add(model_name)
        package_directory = record.get("package_directory")
        if not isinstance(package_directory, str) or not re.fullmatch(
            r"[A-Za-z0-9._-]+", package_directory
        ):
            raise ValueError(f"{field} has an invalid package_directory")

        spec_record = record.get("spec")
        source_record = record.get("source")
        artifact_record = record.get("artifact")
        if not all(
            isinstance(value, dict)
            for value in (spec_record, source_record, artifact_record)
        ):
            raise ValueError(f"{field} has incomplete file records")
        spec_path = verify_file_record(PROJECT_ROOT, spec_record, f"{field}.spec")
        spec = json.loads(spec_path.read_text(encoding="utf-8"))
        spec_name = spec.get("name")
        if spec_name is not None and spec_name != model_name:
            raise ValueError(f"{field} model name differs from its spec")

        source_path = verify_file_record(
            PROJECT_ROOT, source_record, f"{field}.source"
        )
        expected_source_path = repository_path(
            str(spec.get("source_repository_path", "")),
            f"{field}.spec.source_repository_path",
        )
        if source_path != expected_source_path:
            raise ValueError(f"{field} references a different source model")
        if source_record.get("size_bytes") != source_path.stat().st_size:
            raise ValueError(f"{field} source model size mismatch")
        if spec.get("source_sha256") != source_record.get("sha256"):
            raise ValueError(f"{field} source model differs from its spec")

        artifact_path = verify_file_record(
            PROJECT_ROOT, artifact_record, f"{field}.artifact"
        )
        if artifact_path.suffix.lower() != ".rknn":
            raise ValueError(f"{field} artifact must be an .rknn file")
        if artifact_record.get("size_bytes") != artifact_path.stat().st_size:
            raise ValueError(f"{field} artifact size mismatch")
        artifacts.append((model_name, artifact_path))

    return manifest_file, document, artifacts, license_path


def verify_staged_resources(
    raw_profile_path: str | Path, raw_output_dir: str | Path | None = None
) -> dict[str, Any]:
    profile_path, profile, template_root, output_dir, target_token = platform_context(
        raw_profile_path, raw_output_dir
    )
    packaging = profile.get("packaging", {})
    requires_manifest = bool(packaging.get("resource_manifest_required", False))
    requires_manifest = requires_manifest or output_dir != template_root
    if not requires_manifest:
        if not (output_dir / "models").is_dir():
            raise ValueError(f"direct resource template has no models: {output_dir}")
        return {
            "status": "DIRECT_TEMPLATE",
            "chip": profile["chip"],
            "resource_root": output_dir.relative_to(PROJECT_ROOT).as_posix(),
        }

    resource_manifest_path = output_dir / "resource-manifest.json"
    if not resource_manifest_path.is_file():
        raise ValueError(
            f"staged resource manifest is missing: {resource_manifest_path}; "
            "regenerate the platform resource overlay"
        )
    manifest = json.loads(resource_manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema_version") != MANIFEST_SCHEMA_VERSION:
        raise ValueError(
            "staged resource manifest schema is stale: "
            f"expected {MANIFEST_SCHEMA_VERSION}, got {manifest.get('schema_version')}"
        )
    if manifest.get("chip") != profile.get("chip"):
        raise ValueError("staged resource manifest chip does not match the platform profile")
    if manifest.get("directory_token") != target_token:
        raise ValueError("staged resource directory token does not match the platform profile")

    profile_record = manifest.get("platform_profile")
    if not isinstance(profile_record, dict):
        raise ValueError("staged resource manifest has no platform_profile record")
    recorded_profile_path = verify_file_record(
        PROJECT_ROOT, profile_record, "platform_profile"
    )
    if recorded_profile_path != profile_path:
        raise ValueError("staged resource manifest references a different platform profile")

    template_record = manifest.get("resource_template")
    if not isinstance(template_record, dict):
        raise ValueError("staged resource manifest has no resource_template record")
    recorded_template_path = manifest_path(
        PROJECT_ROOT, template_record, "resource_template"
    )
    if recorded_template_path != template_root:
        raise ValueError("staged resource manifest references a different resource template")
    source_token = str(template_record.get("source_chip", ""))
    if not source_token:
        raise ValueError("staged resource manifest has no source chip token")

    artifact_bundle_record = manifest.get("artifact_bundle")
    bundle_models: dict[str, dict[str, Any]] | None = None
    bundle_usage_scope: str | None = None
    if artifact_bundle_record is not None:
        if not isinstance(artifact_bundle_record, dict):
            raise ValueError("staged resource manifest has an invalid artifact_bundle")
        source_manifest_record = artifact_bundle_record.get("source")
        packaged_manifest_record = artifact_bundle_record.get("packaged_manifest")
        packaged_license_record = artifact_bundle_record.get("packaged_license")
        if not all(
            isinstance(value, dict)
            for value in (
                source_manifest_record,
                packaged_manifest_record,
                packaged_license_record,
            )
        ):
            raise ValueError("staged artifact bundle has incomplete file records")
        source_manifest_path = verify_file_record(
            PROJECT_ROOT, source_manifest_record, "artifact_bundle.source"
        )
        (
            loaded_manifest_path,
            bundle_document,
            _,
            source_license_path,
        ) = load_artifact_manifest(source_manifest_path, str(profile.get("chip", "")))
        if loaded_manifest_path != source_manifest_path:
            raise ValueError("staged artifact bundle references a different manifest")
        packaged_manifest_path = verify_file_record(
            output_dir,
            packaged_manifest_record,
            "artifact_bundle.packaged_manifest",
        )
        if packaged_manifest_path.read_bytes() != source_manifest_path.read_bytes():
            raise ValueError("packaged artifact manifest differs from its source")
        packaged_license_path = verify_file_record(
            output_dir,
            packaged_license_record,
            "artifact_bundle.packaged_license",
        )
        if packaged_license_path.read_bytes() != source_license_path.read_bytes():
            raise ValueError("packaged model license differs from its source")
        if packaged_license_record.get("spdx") != bundle_document["license"]["spdx"]:
            raise ValueError("packaged model license SPDX identifier changed")
        bundle_usage_scope = str(bundle_document.get("usage_scope", ""))
        bundle_models = {
            str(record["model"]): record for record in bundle_document["models"]
        }

    model_records = manifest.get("models")
    if not isinstance(model_records, list) or not model_records:
        raise ValueError("staged resource manifest has no model records")
    staged_codes: set[str] = set()
    expected_model_files: set[str] = set()
    seen_models: set[str] = set()
    for index, record in enumerate(model_records):
        field = f"models[{index}]"
        if not isinstance(record, dict):
            raise ValueError(f"resource manifest {field} must be an object")
        model_name = record.get("model")
        if not isinstance(model_name, str) or not model_name or model_name in seen_models:
            raise ValueError(f"resource manifest {field} has an invalid or duplicate model")
        seen_models.add(model_name)
        code = str(record.get("algorithm_code", ""))
        if not code:
            raise ValueError(f"resource manifest {field} has no algorithm_code")
        staged_codes.add(code)

        spec_record = record.get("spec")
        source_record = record.get("source_template")
        config_record = record.get("config")
        artifact_record = record.get("artifact")
        if not all(
            isinstance(value, dict)
            for value in (spec_record, source_record, config_record, artifact_record)
        ):
            raise ValueError(f"resource manifest {field} has incomplete file records")
        spec_path = verify_file_record(PROJECT_ROOT, spec_record, f"{field}.spec")
        spec = json.loads(spec_path.read_text(encoding="utf-8"))
        if str(spec.get("packaging", {}).get("algorithm_code", "")) != code:
            raise ValueError(f"resource manifest {field} model spec changed")

        source_config_path = verify_file_record(
            PROJECT_ROOT, source_record, f"{field}.source_template"
        )
        if source_config_path.parent.parent.parent != template_root:
            raise ValueError(f"resource manifest {field} source template is outside the profile")
        source_config = json.loads(source_config_path.read_text(encoding="utf-8"))
        if str(source_config.get("algorithm_code", "")) != code:
            raise ValueError(f"resource manifest {field} source template changed")

        target_config_path = verify_file_record(
            output_dir, config_record, f"{field}.config"
        )
        target_config = json.loads(target_config_path.read_text(encoding="utf-8"))
        expected_config = replace_platform_tokens(source_config, source_token, target_token)
        expected_config["chip_type"] = target_token
        if target_config != expected_config:
            raise ValueError(
                "staged model config is stale relative to its source template: "
                f"{target_config_path}"
            )

        target_model_path = verify_file_record(
            output_dir, artifact_record, f"{field}.artifact"
        )
        if artifact_record.get("size_bytes") != target_model_path.stat().st_size:
            raise ValueError(f"resource manifest {field} artifact size mismatch")
        if artifact_record.get("source_sha256") != artifact_record.get("sha256"):
            raise ValueError(f"resource manifest {field} artifact differs from its source")
        if bundle_models is not None:
            bundle_model = bundle_models.get(model_name)
            if bundle_model is None:
                raise ValueError(f"resource manifest {field} is absent from its bundle")
            if (
                artifact_record.get("source_sha256")
                != bundle_model["artifact"].get("sha256")
            ):
                raise ValueError(f"resource manifest {field} differs from its bundle")
            expected_model_path = (
                f"models/{bundle_model['package_directory']}/model.rknn"
            )
            if artifact_record.get("path") != expected_model_path:
                raise ValueError(
                    f"resource manifest {field} package directory changed"
                )
        expected_model_files.update(
            {
                target_config_path.relative_to(output_dir).as_posix(),
                target_model_path.relative_to(output_dir).as_posix(),
            }
        )

    actual_model_files = {
        path.relative_to(output_dir).as_posix()
        for pattern in ("*/config.json", "*/model.rknn")
        for path in (output_dir / "models").glob(pattern)
    }
    if actual_model_files != expected_model_files:
        raise ValueError("staged model inventory does not match the resource manifest")
    if bundle_models is not None and seen_models != set(bundle_models):
        raise ValueError("staged model selection does not match the artifact bundle")

    algorithm_records = manifest.get("algorithms")
    skipped_records = manifest.get("skipped_algorithms")
    if not isinstance(algorithm_records, list) or not isinstance(skipped_records, list):
        raise ValueError("staged resource manifest has invalid algorithm records")
    expected_algorithm_files: set[str] = set()
    recorded_sources: set[str] = set()
    for index, record in enumerate(algorithm_records):
        field = f"algorithms[{index}]"
        if not isinstance(record, dict) or not isinstance(record.get("source"), dict):
            raise ValueError(f"resource manifest {field} has no source record")
        source_path = verify_file_record(
            PROJECT_ROOT, record["source"], f"{field}.source"
        )
        target_path = verify_file_record(output_dir, record, field)
        source_document = json.loads(source_path.read_text(encoding="utf-8"))
        target_document = json.loads(target_path.read_text(encoding="utf-8"))
        if target_document != replace_platform_tokens(
            source_document, source_token, target_token
        ):
            raise ValueError(f"staged algorithm config is stale: {target_path}")
        required_codes = algorithm_atomic_codes(source_document)
        if sorted(required_codes) != record.get("atomic_codes"):
            raise ValueError(f"resource manifest {field} atomic codes changed")
        if not required_codes.issubset(staged_codes):
            raise ValueError(f"resource manifest {field} is missing required models")
        recorded_sources.add(source_path.relative_to(PROJECT_ROOT).as_posix())
        expected_algorithm_files.add(target_path.relative_to(output_dir).as_posix())

    for index, record in enumerate(skipped_records):
        field = f"skipped_algorithms[{index}]"
        if not isinstance(record, dict) or not isinstance(record.get("source"), dict):
            raise ValueError(f"resource manifest {field} has no source record")
        source_path = verify_file_record(
            PROJECT_ROOT, record["source"], f"{field}.source"
        )
        required_codes = algorithm_atomic_codes(
            json.loads(source_path.read_text(encoding="utf-8"))
        )
        missing_codes = sorted(required_codes - staged_codes)
        if not missing_codes or missing_codes != record.get("missing_algorithm_codes"):
            raise ValueError(f"resource manifest {field} selection changed")
        recorded_sources.add(source_path.relative_to(PROJECT_ROOT).as_posix())

    current_sources = {
        path.relative_to(PROJECT_ROOT).as_posix()
        for path in (template_root / "algorithm").glob("*.json")
    }
    if recorded_sources != current_sources:
        raise ValueError("source algorithm inventory changed after resource staging")
    actual_algorithm_files = {
        path.relative_to(output_dir).as_posix()
        for path in (output_dir / "algorithm").glob("*.json")
    }
    if actual_algorithm_files != expected_algorithm_files:
        raise ValueError("staged algorithm inventory does not match the resource manifest")

    result = {
        "status": "PASS",
        "chip": profile["chip"],
        "manifest": resource_manifest_path.relative_to(PROJECT_ROOT).as_posix(),
        "models": sorted(seen_models),
        "algorithms": len(algorithm_records),
    }
    if bundle_usage_scope is not None:
        result["usage_scope"] = bundle_usage_scope
    return result


def stage_platform_resources(
    raw_profile_path: str | Path,
    artifacts: list[tuple[str, Path]],
    raw_output_dir: str | Path | None = None,
    force: bool = False,
    raw_artifact_manifest: str | Path | None = None,
) -> dict[str, Any]:
    profile_path, profile, template_root, output_dir, target_token = platform_context(
        raw_profile_path, raw_output_dir
    )
    allowed_output_root = (PROJECT_ROOT / "output" / "platform-artifacts").resolve()
    try:
        output_dir.relative_to(allowed_output_root)
    except ValueError as error:
        raise ValueError(
            "generated platform resources must stay under output/platform-artifacts"
        ) from error
    if output_dir == template_root or template_root in output_dir.parents:
        raise ValueError("generated platform resources must not overwrite the resource template")

    artifact_bundle_path: Path | None = None
    artifact_bundle: dict[str, Any] | None = None
    artifact_license_path: Path | None = None
    if raw_artifact_manifest is not None:
        if artifacts:
            raise ValueError(
                "--artifact-manifest cannot be combined with explicit artifacts"
            )
        (
            artifact_bundle_path,
            artifact_bundle,
            artifacts,
            artifact_license_path,
        ) = load_artifact_manifest(raw_artifact_manifest, str(profile.get("chip", "")))
    if output_dir.exists():
        if not force:
            raise ValueError(f"output already exists: {output_dir}; pass --force to replace it")
        shutil.rmtree(output_dir)

    templates = model_templates(template_root)
    source_tokens = {
        str(config.get("chip_type", ""))
        for _, config in templates.values()
        if config.get("chip_type")
    }
    if len(source_tokens) != 1:
        raise ValueError("template model configs must share one chip_type")
    source_token = next(iter(source_tokens))

    supplied = dict(artifacts)
    if not supplied:
        raise ValueError("at least one --artifact is required when staging resources")
    if len(supplied) != len(artifacts):
        raise ValueError("each --artifact model name must be unique")
    staged_codes: set[str] = set()
    records: list[dict[str, Any]] = []
    for model_name, artifact_path in sorted(supplied.items()):
        bundle_model = None
        if artifact_bundle is not None:
            bundle_model = next(
                record
                for record in artifact_bundle["models"]
                if record["model"] == model_name
            )
            spec_path = repository_path(
                bundle_model["spec"]["path"],
                f"artifact bundle spec for {model_name}",
            )
        else:
            spec_path = (
                PROJECT_ROOT / "config" / "rknn" / "models" / f"{model_name}.json"
            )
        if not spec_path.is_file():
            raise ValueError(f"model spec does not exist: {spec_path}")
        spec = json.loads(spec_path.read_text(encoding="utf-8"))
        code = str(spec.get("packaging", {}).get("algorithm_code", ""))
        if code not in templates:
            raise ValueError(
                f"no resource template for {model_name} algorithm_code={code or 'missing'}"
            )
        source_dir, source_config = templates[code]
        source_config_path = source_dir / "config.json"
        target_dir_name = source_dir.name.replace(source_token, target_token)
        if bundle_model is not None:
            if bundle_model["package_directory"] != target_dir_name:
                raise ValueError(
                    f"artifact bundle package directory changed for {model_name}"
                )
        target_dir = output_dir / "models" / target_dir_name
        target_config = replace_platform_tokens(source_config, source_token, target_token)
        target_config["chip_type"] = target_token
        write_json(target_dir / "config.json", target_config)
        target_model = target_dir / "model.rknn"
        target_model.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(artifact_path, target_model)
        staged_codes.add(code)
        records.append(
            {
                "model": model_name,
                "algorithm_code": code,
                "spec": {
                    "path": spec_path.relative_to(PROJECT_ROOT).as_posix(),
                    "sha256": sha256(spec_path),
                },
                "source_template": {
                    "path": source_config_path.relative_to(PROJECT_ROOT).as_posix(),
                    "sha256": sha256(source_config_path),
                },
                "config": {
                    "path": (target_dir / "config.json").relative_to(output_dir).as_posix(),
                    "sha256": sha256(target_dir / "config.json"),
                },
                "artifact": {
                    "source_sha256": sha256(artifact_path),
                    "path": target_model.relative_to(output_dir).as_posix(),
                    "sha256": sha256(target_model),
                    "size_bytes": target_model.stat().st_size,
                },
            }
        )

    algorithm_records = []
    skipped_algorithms = []
    for source_path in sorted((template_root / "algorithm").glob("*.json")):
        source_document = json.loads(source_path.read_text(encoding="utf-8"))
        source_record = {
            "path": source_path.relative_to(PROJECT_ROOT).as_posix(),
            "sha256": sha256(source_path),
        }
        required_codes = algorithm_atomic_codes(source_document)
        if not required_codes.issubset(staged_codes):
            skipped_algorithms.append(
                {
                    "source": source_record,
                    "missing_algorithm_codes": sorted(required_codes - staged_codes),
                }
            )
            continue
        target_document = replace_platform_tokens(source_document, source_token, target_token)
        target_name = source_path.name.replace(source_token, target_token)
        target_path = output_dir / "algorithm" / target_name
        write_json(target_path, target_document)
        algorithm_records.append(
            {
                "source": source_record,
                "path": target_path.relative_to(output_dir).as_posix(),
                "sha256": sha256(target_path),
                "atomic_codes": sorted(required_codes),
            }
        )

    artifact_bundle_record = None
    if (
        artifact_bundle_path is not None
        and artifact_bundle is not None
        and artifact_license_path is not None
    ):
        packaged_manifest_path = output_dir / "model-bundle.json"
        packaged_license_path = (
            output_dir / "licenses" / "model-assets" / artifact_license_path.name
        )
        packaged_manifest_path.parent.mkdir(parents=True, exist_ok=True)
        packaged_license_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(artifact_bundle_path, packaged_manifest_path)
        shutil.copyfile(artifact_license_path, packaged_license_path)
        artifact_bundle_record = {
            "source": {
                "path": artifact_bundle_path.relative_to(PROJECT_ROOT).as_posix(),
                "sha256": sha256(artifact_bundle_path),
            },
            "packaged_manifest": {
                "path": packaged_manifest_path.relative_to(output_dir).as_posix(),
                "sha256": sha256(packaged_manifest_path),
            },
            "packaged_license": {
                "path": packaged_license_path.relative_to(output_dir).as_posix(),
                "sha256": sha256(packaged_license_path),
                "spdx": artifact_bundle["license"]["spdx"],
            },
            "usage_scope": artifact_bundle["usage_scope"],
        }

    manifest = {
        "schema_version": MANIFEST_SCHEMA_VERSION,
        "created_at": datetime.now(timezone.utc).isoformat(),
        "chip": profile["chip"],
        "directory_token": target_token,
        "platform_profile": {
            "path": profile_path.relative_to(PROJECT_ROOT).as_posix(),
            "sha256": sha256(profile_path),
        },
        "resource_template": {
            "path": template_root.relative_to(PROJECT_ROOT).as_posix(),
            "source_chip": source_token,
        },
        "models": records,
        "algorithms": algorithm_records,
        "skipped_algorithms": skipped_algorithms,
        "artifact_bundle": artifact_bundle_record,
    }
    write_json(output_dir / "resource-manifest.json", manifest)
    verify_staged_resources(profile_path, output_dir)
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--platform-profile", required=True)
    parser.add_argument("--artifact", action="append", type=parse_artifact, default=[])
    parser.add_argument("--artifact-manifest")
    parser.add_argument("--output-dir")
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    try:
        if args.verify:
            if args.artifact or args.artifact_manifest or args.force:
                parser.error(
                    "--verify cannot be combined with --artifact, "
                    "--artifact-manifest, or --force"
                )
            result = verify_staged_resources(args.platform_profile, args.output_dir)
        else:
            result = stage_platform_resources(
                args.platform_profile,
                args.artifact,
                args.output_dir,
                args.force,
                args.artifact_manifest,
            )
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(str(error))
    print(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
