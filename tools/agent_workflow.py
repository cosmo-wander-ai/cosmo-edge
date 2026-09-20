#!/usr/bin/env python3
"""Shared, dependency-free helpers for CosmoEdge agent-assisted workflows."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from secrets import token_hex
from typing import Any, Iterable

import source_toolchain


PROJECT_ROOT = Path(__file__).resolve().parents[1]
RUN_ID_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")
REQUIRED_CONTRACT_FIELDS = (
    "schemaVersion",
    "runId",
    "task",
    "userObjective",
    "expectedDeliverables",
    "allowedChanges",
    "requiredCapabilities",
    "acceptance",
    "authority",
)
ENVIRONMENT_VERDICTS = {"READY", "REPAIRABLE", "NEEDS_ENVIRONMENT", "UNSUPPORTED"}
CHECK_STATUSES = {"PASS", "FAIL", "BLOCKED", "SKIP", "UNVERIFIED"}
ASSESSMENT_VERDICTS = {"READY", "NEEDS_INPUT", "NEEDS_ENVIRONMENT", "UNSUPPORTED"}
SENSITIVE_NAME = (
    r"(?:password|passwd|pwd|token|api[_-]?key|authorization|credential|secret|"
    r"username|user[_-]?name|account|密码|口令|令牌|密钥|凭据|用户名|账号)"
)
SENSITIVE_VALUE_NAME = (
    r"(?:password|passwd|pwd|token|api[_-]?key|credential|secret|"
    r"username|user[_-]?name|account)"
)
PRIVATE_NETWORK_PATTERN = re.compile(
    r"(?<!\d)(?:10(?:\.\d{1,3}){3}|192\.168(?:\.\d{1,3}){2}|"
    r"172\.(?:1[6-9]|2\d|3[01])(?:\.\d{1,3}){2}|127(?:\.\d{1,3}){3})(?!\d)"
)
IMAGE_REFERENCE_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._/@:-]+$")
TOOLCHAIN_KINDS = {"auto", "python-package", "container-image"}
TOOLCHAIN_PACKAGE_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]*$")
AUTHORITY_GRANTS = {
    "environment-change",
    "remote-execution",
    "model-transfer",
    "device-deployment",
}
TPU_MLIR_OFFICIAL_REFERENCE = "https://github.com/sophgo/tpu-mlir#-installation"
RKNN_TOOLKIT2_OFFICIAL_REFERENCE = "https://github.com/airockchip/rknn-toolkit2"
TOOLCHAIN_PROBE_SCRIPT = r"""
import hashlib
import importlib
import importlib.metadata
import json
import os
import pathlib
import shutil
import sys

package = sys.argv[1]
tool_paths = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
family = sys.argv[3] if len(sys.argv) > 3 else "sophon"
module_name = sys.argv[4] if len(sys.argv) > 4 else package.replace("-", "_")
distribution = importlib.metadata.distribution(package)
module = importlib.import_module(module_name)
bin_dir = pathlib.Path(sys.executable).absolute().parent

def tool(key, names, required=True):
    def record(path, resolution):
        path = path.resolve()
        head = path.read_bytes()[:256]
        first_line = head.splitlines()[0].lower() if head else b""
        invocation = (
            "python"
            if path.suffix.lower() == ".py" or b"python" in first_line
            else "direct"
        )
        return {
            "path": str(path),
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "resolution": resolution,
            "invocation": invocation,
        }
    declared = tool_paths.get(key)
    if declared:
        candidate = pathlib.Path(declared)
        if not candidate.is_absolute():
            resolved = shutil.which(declared)
            candidate = pathlib.Path(resolved) if resolved else candidate
        if candidate.is_file():
            return record(candidate, "declared")
        raise RuntimeError("declared tool is unavailable: " + str(declared))
    for name in names:
        candidate = bin_dir / name
        if candidate.is_file():
            return record(candidate, "python-bin")
        resolved = shutil.which(name)
        if resolved:
            return record(pathlib.Path(resolved), "path")
    if not required:
        return None
    raise RuntimeError("missing tool: " + " or ".join(names))

record = None
for entry in distribution.files or []:
    if str(entry).endswith(".dist-info/RECORD"):
        record = pathlib.Path(distribution.locate_file(entry)).resolve()
        break
if record is None or not record.is_file():
    raise RuntimeError("installed distribution has no RECORD identity")

package_root = pathlib.Path(module.__file__).resolve().parent
runtime_links = {}
broken_links = {}
for path in package_root.rglob("*"):
    if not path.is_symlink():
        continue
    relative = str(path.relative_to(package_root))
    if not path.exists():
        broken_links[relative] = os.readlink(path)
        continue
    target = path.resolve()
    item = {"target": os.readlink(path)}
    if target.is_file():
        item["targetSha256"] = hashlib.sha256(target.read_bytes()).hexdigest()
    runtime_links[relative] = item
required_runtime_links = (
    {
        "lib/libcmodel.so",
        "lib/libbmlib.so",
        "lib/libbmlib.so.0",
    }
    if family == "sophon"
    else set()
)
broken_required = [
    relative + " -> " + target
    for relative, target in broken_links.items()
    if relative in required_runtime_links
]
if broken_required:
    raise RuntimeError(
        "installed distribution has broken required links: " + "; ".join(broken_required)
    )

if family == "rknn":
    rknn_class = getattr(module, "RKNN", None)
    if rknn_class is None:
        raise RuntimeError("rknn.api does not expose RKNN")
    missing_methods = [
        name
        for name in ("config", "load_onnx", "build", "export_rknn", "release")
        if not callable(getattr(rknn_class, name, None))
    ]
    if missing_methods:
        raise RuntimeError("RKNN API is missing methods: " + ", ".join(missing_methods))
    tools = {}
else:
    tools = {
        "modelTransform": tool("modelTransform", ["model_transform.py", "model_transform"]),
        "modelDeploy": tool("modelDeploy", ["model_deploy.py", "model_deploy"]),
        "modelTool": tool("modelTool", ["model_tool"], required=False),
    }

print(json.dumps({
    "family": family,
    "pythonExecutable": str(pathlib.Path(sys.executable).absolute()),
    "pythonVersion": sys.version.split()[0],
    "sysPrefix": str(pathlib.Path(sys.prefix).absolute()),
    "basePrefix": str(pathlib.Path(getattr(sys, "base_prefix", sys.prefix)).absolute()),
    "requiresPython": distribution.metadata.get("Requires-Python"),
    "package": {
        "name": distribution.metadata.get("Name") or package,
        "version": distribution.version,
        "recordSha256": hashlib.sha256(record.read_bytes()).hexdigest(),
    },
    "module": {"name": module_name, "path": str(pathlib.Path(module.__file__).resolve())},
    "tools": tools,
    "runtimeLinks": runtime_links,
    "brokenOptionalLinks": broken_links,
}, sort_keys=True))
"""


class WorkflowError(ValueError):
    """Raised for invalid contracts or unsafe workflow paths."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _require_nonempty_string(value: Any, field: str) -> None:
    if not isinstance(value, str) or not value.strip():
        raise WorkflowError(f"{field} must be a non-empty string")


def validate_contract(data: Any) -> dict[str, Any]:
    if not isinstance(data, dict):
        raise WorkflowError("task contract must be a JSON object")
    missing = [field for field in REQUIRED_CONTRACT_FIELDS if field not in data]
    if missing:
        raise WorkflowError(f"task contract is missing required fields: {', '.join(missing)}")
    if data["schemaVersion"] != "1.0":
        raise WorkflowError("schemaVersion must be 1.0")
    _require_nonempty_string(data["runId"], "runId")
    if not RUN_ID_PATTERN.fullmatch(data["runId"]):
        raise WorkflowError("runId must contain only letters, numbers, dot, underscore, or dash")
    _require_nonempty_string(data["task"], "task")
    _require_nonempty_string(data["userObjective"], "userObjective")
    if (
        not isinstance(data["expectedDeliverables"], list)
        or not data["expectedDeliverables"]
        or any(not isinstance(item, str) or not item.strip() for item in data["expectedDeliverables"])
    ):
        raise WorkflowError("expectedDeliverables must be a non-empty string array")
    for field in ("allowedChanges", "requiredCapabilities"):
        if not isinstance(data[field], list):
            raise WorkflowError(f"{field} must be an array")
    if any(not isinstance(item, str) or not item.strip() for item in data["allowedChanges"]):
        raise WorkflowError("allowedChanges entries must be non-empty strings")
    if any(not isinstance(item, (str, dict)) for item in data["requiredCapabilities"]):
        raise WorkflowError("requiredCapabilities entries must be strings or objects")
    if not isinstance(data["acceptance"], dict):
        raise WorkflowError("acceptance must be an object")
    if not isinstance(data["authority"], dict):
        raise WorkflowError("authority must be an object")
    _require_nonempty_string(data["authority"].get("workspace"), "authority.workspace")
    grants = data["authority"].get("grants", [])
    if not isinstance(grants, list) or any(not isinstance(item, str) for item in grants):
        raise WorkflowError("authority.grants must be an array of grant names")
    unknown_grants = sorted(set(grants) - AUTHORITY_GRANTS)
    if unknown_grants:
        raise WorkflowError(f"authority.grants contains unknown grants: {', '.join(unknown_grants)}")
    if "parameters" in data and not isinstance(data["parameters"], dict):
        raise WorkflowError("parameters must be an object when present")
    if "extensions" in data and not isinstance(data["extensions"], dict):
        raise WorkflowError("extensions must be an object when present")
    return data


def load_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as error:
        raise WorkflowError(f"file does not exist: {path}") from error
    except json.JSONDecodeError as error:
        raise WorkflowError(f"invalid JSON in {path}: {error}") from error


def _is_relative_to(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def resolve_contract_context(
    contract_arg: str | os.PathLike[str], project_root: Path = PROJECT_ROOT
) -> tuple[Path, Path, dict[str, Any]]:
    root = project_root.resolve()
    candidate = Path(contract_arg).expanduser()
    if not candidate.is_absolute():
        candidate = root / candidate
    try:
        contract_path = candidate.resolve(strict=True)
    except FileNotFoundError as error:
        raise WorkflowError(f"contract does not exist: {candidate}") from error
    runs_root = (root / "output" / "agent-runs").resolve()
    if not _is_relative_to(contract_path, runs_root):
        raise WorkflowError("contract must stay under output/agent-runs/<run-id>/")
    run_dir = contract_path.parent
    relative = contract_path.relative_to(runs_root)
    if len(relative.parts) != 2 or relative.parts[1] != "task-contract.json":
        raise WorkflowError("contract path must be output/agent-runs/<run-id>/task-contract.json")
    data = validate_contract(load_json(contract_path))
    if data["runId"] != run_dir.name:
        raise WorkflowError("runId must match the contract directory name")
    return contract_path, run_dir, data


def resolve_run_input(run_dir: Path, raw_path: str, *, must_exist: bool = True) -> Path:
    _require_nonempty_string(raw_path, "run input path")
    candidate = Path(raw_path).expanduser()
    if not candidate.is_absolute():
        candidate = run_dir / candidate
    try:
        resolved = candidate.resolve(strict=must_exist)
    except FileNotFoundError as error:
        raise WorkflowError(f"run input does not exist: {raw_path}") from error
    if not _is_relative_to(resolved, run_dir.resolve()):
        raise WorkflowError("run input path escapes the current run directory")
    if must_exist and not resolved.is_file():
        raise WorkflowError(f"run input must be a file: {raw_path}")
    return resolved


def redact_text(text: str) -> str:
    redacted = re.sub(
        r"(?i)\b([a-z][a-z0-9+.-]*://)[^/\s:@]+:[^@\s/]+@",
        r"\1[REDACTED]@",
        text,
    )
    redacted = re.sub(
        rf"(?i)((?<!\w)--?{SENSITIVE_NAME})(=|\s+)([^\s]+)",
        r"\1\2[REDACTED]",
        redacted,
    )
    redacted = re.sub(
        rf"""(?i)((?<![\w?&]){SENSITIVE_NAME}\b\s*=\s*)([^\s"'<>]+)""",
        r"\1[REDACTED]",
        redacted,
    )
    redacted = re.sub(
        rf"""(?i)([?&]{SENSITIVE_NAME}=)[^&#\s"'<>]+""",
        r"\1[REDACTED]",
        redacted,
    )
    redacted = re.sub(
        r"(?i)(\bBearer\s+)[A-Za-z0-9._~+/=-]+",
        r"\1[REDACTED]",
        redacted,
    )
    redacted = re.sub(
        rf"(?i)((?<!\w){SENSITIVE_VALUE_NAME}\s*[:=]\s*)([A-Za-z0-9._~+/@=-]{{2,}})",
        r"\1[REDACTED]",
        redacted,
    )
    redacted = re.sub(
        r"((?:密码|口令|令牌|密钥|凭据|用户名|账号)\s*(?:是|为)?\s*[:：=]?\s*)"
        r"([A-Za-z0-9._~+/@=-]{2,})",
        r"\1[REDACTED]",
        redacted,
    )
    return PRIVATE_NETWORK_PATTERN.sub("[PRIVATE_TARGET]", redacted)


def redact_data(value: Any, key: str = "") -> Any:
    if key and re.search(SENSITIVE_NAME, key, flags=re.IGNORECASE):
        return "[REDACTED]" if value not in (None, "", False) else value
    if isinstance(value, dict):
        return {item_key: redact_data(item_value, str(item_key)) for item_key, item_value in value.items()}
    if isinstance(value, list):
        return [redact_data(item) for item in value]
    if isinstance(value, str):
        return redact_text(value)
    return value


def _run(
    command: list[str],
    *,
    cwd: Path = PROJECT_ROOT,
    timeout: int = 8,
    env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            command,
            cwd=cwd,
            text=True,
            capture_output=True,
            check=False,
            timeout=timeout,
            env=env,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        return subprocess.CompletedProcess(command, 127, "", str(error))


def _first_line(value: str, limit: int = 240) -> str:
    lines = [line.strip() for line in value.splitlines() if line.strip()]
    return (lines[0] if lines else "")[:limit]


def _last_line(value: str, limit: int = 240) -> str:
    lines = [line.strip() for line in value.splitlines() if line.strip()]
    return (lines[-1] if lines else "")[:limit]


def _tool_inventory(name: str, version_args: Iterable[str] = ("--version",)) -> dict[str, Any]:
    executable = shutil.which(name)
    if not executable:
        return {"available": False}
    process = _run([executable, *version_args])
    output = _first_line(process.stdout) or _first_line(process.stderr)
    return {
        "available": True,
        "path": executable,
        "version": redact_text(output) if output else None,
        "versionExitCode": process.returncode,
    }


def _git_snapshot() -> dict[str, Any]:
    commit = _run(["git", "rev-parse", "HEAD"])
    tree = _run(["git", "rev-parse", "HEAD^{tree}"])
    branch = _run(["git", "branch", "--show-current"])
    status = _run(["git", "status", "--porcelain", "--untracked-files=normal"])
    tracked_diff = _run(["git", "diff", "--binary", "HEAD"], timeout=30)
    tracked_lines = [
        line for line in status.stdout.splitlines() if line and not line.startswith("??")
    ]
    untracked_lines = [line for line in status.stdout.splitlines() if line.startswith("??")]
    fingerprint_payload = (
        tracked_diff.stdout.encode("utf-8", errors="replace")
        + b"\0"
        + "\n".join(untracked_lines).encode("utf-8", errors="replace")
    )
    return {
        "commit": _first_line(commit.stdout) or None,
        "tree": _first_line(tree.stdout) or None,
        "branch": _first_line(branch.stdout) or None,
        "trackedChanges": len(tracked_lines),
        "untrackedFiles": len(untracked_lines),
        "worktreeFingerprint": hashlib.sha256(fingerprint_payload).hexdigest(),
    }


def _memory_bytes() -> tuple[int | None, int | None]:
    total = None
    available = None
    try:
        page_size = os.sysconf("SC_PAGE_SIZE")
        total = int(page_size * os.sysconf("SC_PHYS_PAGES"))
        if "SC_AVPHYS_PAGES" in os.sysconf_names:
            available = int(page_size * os.sysconf("SC_AVPHYS_PAGES"))
    except (AttributeError, OSError, ValueError):
        pass
    meminfo = Path("/proc/meminfo")
    if meminfo.is_file():
        values: dict[str, int] = {}
        for line in meminfo.read_text(encoding="utf-8").splitlines():
            key, _, value = line.partition(":")
            match = re.search(r"\d+", value)
            if match:
                values[key] = int(match.group()) * 1024
        total = values.get("MemTotal", total)
        available = values.get("MemAvailable", available)
    return total, available


def host_inventory(project_root: Path = PROJECT_ROOT) -> dict[str, Any]:
    total_memory, available_memory = _memory_bytes()
    disk = shutil.disk_usage(project_root)
    tools = {
        name: _tool_inventory(name)
        for name in ("git", "python3", "docker", "cmake", "make", "g++", "node", "npm")
    }
    return {
        "host": {
            "os": platform.system(),
            "osRelease": platform.release(),
            "architecture": platform.machine(),
            "cpuCount": os.cpu_count(),
            "memoryBytes": {"total": total_memory, "available": available_memory},
            "diskBytes": {"total": disk.total, "free": disk.free},
        },
        "repository": _git_snapshot(),
        "tools": tools,
    }


def baseline_report(project_root: Path = PROJECT_ROOT) -> dict[str, Any]:
    inventory = host_inventory(project_root)
    return {
        "schemaVersion": "1.0",
        "mode": "baseline",
        "commit": inventory["repository"]["commit"],
        "authority": {
            "scope": "read-only environment and repository inventory",
            "environmentChanges": False,
            "externalSystems": False,
        },
        **inventory,
        "unknowns": [
            "Task-specific requirements are not evaluated until a task contract is supplied.",
            "Device and production environments are not inspected by baseline mode.",
        ],
    }


def _objective_target_chip(contract: dict[str, Any]) -> str | None:
    parameters = contract.get("parameters", {})
    target = parameters.get("targetChip")
    if isinstance(target, str) and target.strip():
        return target.strip().lower()
    device = parameters.get("device")
    if isinstance(device, dict):
        mapped = device.get("targetChip")
        if isinstance(mapped, str) and mapped.strip():
            return mapped.strip().lower()
    objective = str(contract.get("userObjective", ""))
    match = re.search(r"(?i)\b((?:bm|cv|rk|rv)\d+[a-z0-9]*)\b", objective)
    return match.group(1).lower() if match else None


def _conversion_toolchain_family(target_chip: str | None) -> str | None:
    """Map a target chip to its compiler family without implying product support."""
    normalized = str(target_chip or "").strip().lower()
    if normalized.startswith(("rk", "rv")):
        return "rknn"
    if normalized.startswith(("bm", "cv")):
        return "sophon"
    return None


def _conversion_toolchain_label(family: str | None) -> str:
    return "RKNN Toolkit2" if family == "rknn" else "TPU-MLIR"


def _material_observations(contract: dict[str, Any], run_dir: Path) -> list[dict[str, Any]]:
    parameters = contract.get("parameters", {})
    declared = parameters.get("sourceModel")
    candidates: list[Path] = []
    if isinstance(declared, str) and declared.strip():
        try:
            candidates.append(resolve_run_input(run_dir, declared.strip()))
        except WorkflowError as error:
            return [{"kind": "model", "status": "MISSING", "detail": str(error)}]
    else:
        inputs = run_dir / "inputs"
        if inputs.is_dir():
            candidates.extend(
                path.resolve()
                for path in sorted(inputs.iterdir())
                if path.is_file()
                and path.suffix.lower() in {".onnx", ".pt", ".pth", ".mlir", ".bmodel"}
            )
    observations = []
    for path in candidates:
        observations.append(
            {
                "kind": "model",
                "status": "AVAILABLE",
                "format": path.suffix.lower().lstrip(".") or "unknown",
                "path": str(path.relative_to(run_dir.resolve())),
                "sha256": sha256_file(path),
                "sizeBytes": path.stat().st_size,
            }
        )
    return observations


def _authority_grants(contract: dict[str, Any]) -> set[str]:
    authority = contract.get("authority", {})
    grants = {
        str(item)
        for item in authority.get("grants", [])
        if isinstance(item, str) and item in AUTHORITY_GRANTS
    }
    if authority.get("externalSystems") is True:
        grants.update({"remote-execution", "model-transfer", "device-deployment"})
    if authority.get("environmentChanges") is True:
        grants.add("environment-change")
    return grants


def _needs_input(
    question_id: str,
    question: str,
    reason: str,
    *,
    category: str = "business-input",
    required_before: str = "execution",
) -> dict[str, Any]:
    return {
        "id": question_id,
        "category": category,
        "question": question,
        "reason": reason,
        "requiredBefore": required_before,
    }


def assess_task_report(
    contract_path: Path,
    run_dir: Path,
    contract: dict[str, Any],
    project_root: Path = PROJECT_ROOT,
) -> dict[str, Any]:
    """Compile user intent and available materials into a route without changing state."""
    inventory = host_inventory(project_root)
    materials = _material_observations(contract, run_dir)
    needs_input: list[dict[str, Any]] = []
    route_candidates: list[dict[str, Any]] = []
    recommended_route: str | None = None
    verdict = "READY"

    if contract["task"] == "model-conversion":
        available_models = [item for item in materials if item.get("status") == "AVAILABLE"]
        if not available_models:
            needs_input.append(
                _needs_input(
                    "source-model",
                    "请提供需要适配的模型文件，并说明它来自哪个训练框架或导出流程。",
                    "没有模型物料就无法检查格式、输入输出和可行转换路径。",
                )
            )
        source_format = available_models[0].get("format") if available_models else None
        declared_source = contract.get("parameters", {}).get("sourceModel")
        if len(available_models) > 1 and not (
            isinstance(declared_source, str) and declared_source.strip()
        ):
            needs_input.append(
                _needs_input(
                    "source-model-selection",
                    "发现多个候选模型文件。请说明哪一个是本次要适配的源模型。",
                    "不同源模型会产生不同交付物，智能体不能按文件名排序替用户决定。",
                )
            )
        if source_format and source_format != "onnx":
            needs_input.append(
                _needs_input(
                    "onnx-material",
                    "当前物料不是 ONNX。请提供可用的 ONNX，或授权智能体把“从原训练工程导出 ONNX”作为单独交付阶段评估。",
                    "本版本只执行 ONNX 到目标加速器产物的转换，不能把未实现的导出步骤当作已经支持。",
                    required_before="route",
                )
            )

        target_chip = _objective_target_chip(contract)
        toolchain_family = _conversion_toolchain_family(target_chip) or (
            "sophon" if not target_chip else None
        )
        if not target_chip:
            needs_input.append(
                _needs_input(
                    "target-device",
                    "请说明最终要运行模型的测试设备型号，或提供不含序列号和凭据的设备信息。",
                    "目标设备会改变产物，智能体不能仅凭示例替用户决定芯片映射。",
                )
            )
        elif not toolchain_family:
            needs_input.append(
                _needs_input(
                    "target-toolchain",
                    "当前目标芯片尚未映射到仓库支持的模型编译工具链，请提供官方工具链依据或调整目标。",
                    "未知芯片不能默认套用 Sophon 或 Rockchip 的转换路径。",
                    required_before="route",
                )
            )

        host_os = str(inventory["host"]["os"])
        host_arch = str(inventory["host"]["architecture"]).lower()
        linux_local = host_os == "Linux" and host_arch in {"x86_64", "amd64"}
        parameters = contract.get("parameters", {})
        environment = parameters.get("developmentEnvironment", {})
        remote_linux = isinstance(environment, dict) and (
            str(environment.get("os", "")).lower() == "linux"
            and str(environment.get("architecture", "x86_64")).lower() in {"x86_64", "amd64"}
        )
        if toolchain_family:
            route_suffix = "rknn-toolkit2" if toolchain_family == "rknn" else "tpu-mlir"
            toolchain_label = _conversion_toolchain_label(toolchain_family)
            official_reference = (
                RKNN_TOOLKIT2_OFFICIAL_REFERENCE
                if toolchain_family == "rknn"
                else TPU_MLIR_OFFICIAL_REFERENCE
            )
            route_candidates.append(
                {
                    "id": f"local-linux-{route_suffix}",
                    "title": f"在隔离的 Linux x86_64 开发环境中使用 {toolchain_label}",
                    "eligibility": "ELIGIBLE" if linux_local else "NEEDS_ENVIRONMENT",
                    "officialReference": official_reference,
                    "detail": (
                        "当前宿主满足已选工具链的操作系统与架构方向；后续仍需 doctor 核验实际能力。"
                        if linux_local
                        else (
                            "当前宿主不是 Linux x86_64；这不等于 CosmoEdge 不支持 Windows，而是该 Sophon 工具链路径需要 Linux。"
                            if toolchain_family == "sophon"
                            else f"当前宿主不是 Linux x86_64；这不等于 CosmoEdge 不支持当前宿主，而是本次 {toolchain_label} 路径在隔离 Linux 环境执行。"
                        )
                    ),
                }
            )
            if not linux_local:
                route_candidates.append(
                    {
                        "id": f"remote-linux-{route_suffix}",
                        "title": f"从当前机器编排隔离的 Linux x86_64 {toolchain_label} 环境",
                        "eligibility": "ELIGIBLE" if remote_linux else "NEEDS_ENVIRONMENT",
                        "officialReference": official_reference,
                        "detail": "当前机器保留为材料整理和任务编排入口，转换在隔离 Linux 开发环境执行。",
                    }
                )
            recommended_route = (
                f"local-linux-{route_suffix}" if linux_local else f"remote-linux-{route_suffix}"
            )

        required_grants: set[str] = set()
        if not linux_local and remote_linux:
            required_grants.add("remote-execution")
            if available_models:
                required_grants.add("model-transfer")
        missing_grants = sorted(required_grants - _authority_grants(contract))
        if missing_grants:
            names = "、".join(missing_grants)
            needs_input.append(
                _needs_input(
                    "route-authority",
                    f"推荐路径需要新增授权：{names}。请确认仅对本次隔离开发任务授予这些权限。",
                    "远程执行和模型传输不会从开发工作区权限自动继承。",
                    category="authority",
                    required_before="remote-action",
                )
            )

        if not linux_local and not remote_linux:
            verdict = "NEEDS_ENVIRONMENT"
            toolchain_label = _conversion_toolchain_label(toolchain_family)
            needs_input.append(
                _needs_input(
                    "linux-development-environment",
                    "请提供一台隔离的 Linux x86_64 开发环境，或允许智能体先给出可复用的 Docker/远程 Linux 准备方案；不要在生产设备上补环境。",
                    f"本次 {toolchain_label} 路径以隔离 Linux 环境为执行面，当前宿主只作为编排入口。",
                    category="environment",
                    required_before="doctor",
                )
            )
        elif needs_input:
            verdict = "NEEDS_INPUT"
    else:
        route_candidates.append(
            {
                "id": "repository-native",
                "title": "仓库原生开发与验证路径",
                "eligibility": "ELIGIBLE",
                "detail": "使用与交付物最接近的现有代码、示例和原生测试命令。",
            }
        )
        recommended_route = "repository-native"

    if verdict not in ASSESSMENT_VERDICTS:
        raise WorkflowError(f"unknown assessment verdict: {verdict}")
    return {
        "schemaVersion": "1.0",
        "mode": "assessment",
        "task": contract["task"],
        "runId": contract["runId"],
        "contractSha256": sha256_file(contract_path),
        "userObjective": redact_text(contract["userObjective"]),
        "host": inventory["host"],
        "repository": inventory["repository"],
        "materialObservations": materials,
        "routeCandidates": route_candidates,
        "recommendedRoute": recommended_route,
        "needsInput": needs_input,
        "routeVerdict": verdict,
    }


def read_route_assessment(
    contract_path: Path,
    run_dir: Path,
    contract: dict[str, Any],
    *,
    require_ready: bool = True,
) -> dict[str, Any]:
    """Load the assessment that admits this exact contract to the next stage."""
    report_path = run_dir / "route-assessment.json"
    report = load_json(report_path)
    if not isinstance(report, dict):
        raise WorkflowError("route-assessment.json must be an object")
    if report.get("schemaVersion") != "1.0" or report.get("mode") != "assessment":
        raise WorkflowError("route assessment has an unsupported schema or mode; rerun assess")
    if report.get("runId") != contract["runId"] or report.get("task") != contract["task"]:
        raise WorkflowError("route assessment does not belong to this task contract")
    if report.get("contractSha256") != sha256_file(contract_path):
        raise WorkflowError("task contract changed after route assessment; rerun assess")
    needs_input = report.get("needsInput")
    if not isinstance(needs_input, list):
        raise WorkflowError("route assessment needsInput must be an array")
    verdict = report.get("routeVerdict")
    if verdict not in ASSESSMENT_VERDICTS:
        raise WorkflowError("route assessment has an unknown verdict; rerun assess")
    if require_ready and (verdict != "READY" or needs_input):
        raise WorkflowError(
            "route assessment is not READY; resolve needsInput and rerun assess before doctor"
        )
    return report


def _check(
    check_id: str,
    status: str,
    detail: str,
    *,
    remediation: str = "",
    outcome: str | None = None,
    owner: str | None = None,
) -> dict[str, Any]:
    if status not in CHECK_STATUSES:
        raise WorkflowError(f"unknown check status: {status}")
    result: dict[str, Any] = {"id": check_id, "status": status, "detail": detail}
    if remediation:
        result["remediation"] = remediation
    if owner:
        result["owner"] = owner
    if outcome:
        result["_outcome"] = outcome
    return result


def environment_verdict(checks: Iterable[dict[str, Any]]) -> str:
    outcomes = {item.get("_outcome") for item in checks}
    if "unsupported" in outcomes:
        return "UNSUPPORTED"
    if "needs_environment" in outcomes:
        return "NEEDS_ENVIRONMENT"
    if "repairable" in outcomes:
        return "REPAIRABLE"
    return "READY"


def _required_tool_names(contract: dict[str, Any]) -> list[str]:
    names = []
    for entry in contract["requiredCapabilities"]:
        if isinstance(entry, str):
            names.append(entry)
        elif isinstance(entry.get("tool"), str):
            names.append(entry["tool"])
    return list(dict.fromkeys(names))


def _docker_image_identity(image: str) -> tuple[dict[str, Any] | None, str]:
    process = _run(["docker", "image", "inspect", image], timeout=15)
    if process.returncode != 0:
        return None, _first_line(process.stderr) or "image is not available locally"
    try:
        payload = json.loads(process.stdout)[0]
    except (json.JSONDecodeError, IndexError, TypeError):
        return None, "docker returned an unreadable image inspection result"
    return {
        "reference": image,
        "id": payload.get("Id"),
        "repoDigests": payload.get("RepoDigests") or [],
        "architecture": payload.get("Architecture"),
        "os": payload.get("Os"),
    }, ""


def _resolve_executable(raw_value: str) -> str | None:
    candidate = Path(raw_value).expanduser()
    if candidate.is_absolute():
        return str(candidate.absolute()) if candidate.is_file() else None
    return shutil.which(raw_value)


def _toolchain_spec(parameters: dict[str, Any]) -> tuple[dict[str, Any] | None, str]:
    family = _conversion_toolchain_family(str(parameters.get("targetChip", ""))) or "sophon"
    default_package = "rknn-toolkit2" if family == "rknn" else "tpu_mlir"
    default_module = "rknn.api" if family == "rknn" else "tpu_mlir"
    official_reference = (
        RKNN_TOOLKIT2_OFFICIAL_REFERENCE if family == "rknn" else TPU_MLIR_OFFICIAL_REFERENCE
    )
    raw = parameters.get("toolchain")
    if not isinstance(raw, dict):
        if "toolchain" in parameters and raw is not None:
            return None, "parameters.toolchain must be an object when supplied."
        legacy = parameters.get("toolchainImage")
        if legacy:
            return None, (
                "parameters.toolchainImage identifies only an execution image, not the "
                "complete compiler package."
            )
        raw = {"kind": "auto"}
    kind = raw.get("kind", "auto")
    if kind not in TOOLCHAIN_KINDS:
        return None, "parameters.toolchain.kind must be auto, python-package, or container-image."
    layout = raw.get("layout", "python-package")
    if layout not in {"python-package", "source-tree"}:
        return None, "parameters.toolchain.layout must be python-package or source-tree."
    if layout == "source-tree" and (kind != "container-image" or family != "sophon"):
        return None, "source-tree layout requires a Sophon container-image toolchain."
    if layout == "source-tree" and "package" in raw:
        return None, "source-tree layout freezes compiler files, not a Python distribution package."
    if layout == "source-tree":
        default_module = "pymlir"
    package = str(raw.get("package", default_package)).strip()
    if not TOOLCHAIN_PACKAGE_PATTERN.fullmatch(package):
        return None, "parameters.toolchain.package contains unsupported characters."
    module = str(raw.get("module", default_module)).strip()
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*", module):
        return None, "parameters.toolchain.module must be a valid Python module name."
    version = raw.get("version")
    if version is not None and (not isinstance(version, str) or not version.strip()):
        return None, "parameters.toolchain.version must be a non-empty version requirement."
    normalized: dict[str, Any] = {
        "family": family,
        "kind": kind,
        "package": package,
        "module": module,
        "version": version.strip() if isinstance(version, str) else None,
        "officialReference": official_reference,
    }
    tool_paths = raw.get("toolPaths", {})
    if not isinstance(tool_paths, dict) or any(
        key not in {"modelTransform", "modelDeploy", "modelTool"}
        or not isinstance(value, str)
        or not value.strip()
        for key, value in tool_paths.items()
    ):
        return None, "parameters.toolchain.toolPaths must contain supported non-empty command paths."
    normalized["toolPaths"] = {key: value.strip() for key, value in tool_paths.items()}
    if layout == "source-tree":
        try:
            normalized["sourceTree"] = source_toolchain.normalize(raw)
        except ValueError as error:
            return None, str(error)
        normalized["layout"] = layout
        normalized.pop("package")
    if kind in {"auto", "python-package"}:
        executable = raw.get("pythonExecutable")
        if executable is not None and (not isinstance(executable, str) or not executable.strip()):
            return None, "parameters.toolchain.pythonExecutable must be non-empty when supplied."
        normalized["pythonExecutable"] = executable.strip() if isinstance(executable, str) else None
    else:
        image = raw.get("image")
        if (
            not isinstance(image, str)
            or not IMAGE_REFERENCE_PATTERN.fullmatch(image)
            or "://" in image
        ):
            return None, "container-image toolchains require a safe parameters.toolchain.image."
        normalized["image"] = image
    return normalized, ""


def _identity_digest(payload: dict[str, Any]) -> str:
    canonical = json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return "sha256:" + hashlib.sha256(canonical).hexdigest()


def _parse_toolchain_probe(
    process: subprocess.CompletedProcess[str],
    *,
    kind: str,
    image: dict[str, Any] | None = None,
) -> tuple[dict[str, Any] | None, str]:
    if process.returncode != 0:
        return None, _last_line(process.stderr) or "compiler package inspection failed"
    try:
        payload = json.loads(process.stdout)
    except json.JSONDecodeError:
        return None, "compiler package inspection returned unreadable output"
    if not isinstance(payload, dict):
        return None, "compiler package inspection returned an invalid identity"
    identity: dict[str, Any] = {"kind": kind, **payload}
    if image is not None:
        identity["image"] = image
    identity["id"] = _identity_digest(identity)
    return identity, ""


def inspect_toolchain(specification: dict[str, Any]) -> tuple[dict[str, Any] | None, str]:
    package = str(specification.get("package", "tpu_mlir"))
    expected_version = specification.get("version")
    tool_paths = json.dumps(specification.get("toolPaths", {}), sort_keys=True)
    family = str(specification.get("family", "sophon"))
    module = str(specification.get("module", package.replace("-", "_")))
    if specification["kind"] in {"auto", "python-package"}:
        declared = specification.get("pythonExecutable")
        candidates = [declared] if declared else [sys.executable, "python3"]
        errors = []
        identity = None
        error = ""
        seen_executables: set[str] = set()
        for candidate in dict.fromkeys(str(value) for value in candidates if value):
            executable = _resolve_executable(candidate)
            if not executable:
                errors.append(f"Python is unavailable: {candidate}")
                continue
            executable_key = str(Path(executable).resolve())
            if executable_key in seen_executables:
                continue
            seen_executables.add(executable_key)
            process = _run(
                [executable, "-c", TOOLCHAIN_PROBE_SCRIPT, package, tool_paths, family, module],
                timeout=90,
            )
            identity, error = _parse_toolchain_probe(process, kind="python-package")
            if identity:
                identity["selection"] = (
                    "declared-python" if declared else "discovered-python"
                )
                break
            errors.append(f"{candidate}: {error}")
        if not identity:
            return None, "; ".join(errors) or "no Python environment could be inspected"
    else:
        image, image_error = _docker_image_identity(str(specification["image"]))
        if not image:
            return None, (
                f"declared toolchain image {specification['image']} is unavailable: {image_error}"
            )
        source_layout = specification.get("layout") == "source-tree"
        if source_layout:
            try:
                source_toolchain.validate_host_root(specification)
            except ValueError as error:
                return None, str(error)
        probe_arguments = (
            [source_toolchain.PROBE_SCRIPT, specification["sourceTree"]["sourceMount"], tool_paths, module]
            if source_layout else [TOOLCHAIN_PROBE_SCRIPT, package, tool_paths, family, module]
        )
        process = _run(
            [
                "docker",
                "run",
                "--rm",
                "--network",
                "none",
                *source_toolchain.docker_arguments(specification),
                "--entrypoint",
                "python3",
                str(image["id"]),
                "-c",
                *probe_arguments,
            ],
            timeout=300 if source_layout else 120,
        )
        identity, error = _parse_toolchain_probe(
            process,
            kind="container-image",
            image=image,
        )
        if identity and source_layout:
            identity["sourceTree"] = specification["sourceTree"]
    if not identity:
        return None, error
    identity["officialReference"] = specification.get(
        "officialReference",
        RKNN_TOOLKIT2_OFFICIAL_REFERENCE if family == "rknn" else TPU_MLIR_OFFICIAL_REFERENCE,
    )
    actual_version = str(identity.get("compiler", identity.get("package", {})).get("version", ""))
    if not _version_satisfies(actual_version, expected_version):
        return None, (
            f"{package}={actual_version or 'unknown'} does not satisfy "
            f"{expected_version}"
        )
    identity.pop("id", None)
    identity["id"] = _identity_digest(identity)
    return identity, ""


def toolchain_environment(identity: dict[str, Any]) -> dict[str, str] | None:
    if identity.get("kind") != "python-package":
        return None
    python_executable = identity.get("pythonExecutable")
    if not isinstance(python_executable, str) or not python_executable:
        return None
    environment = os.environ.copy()
    path_entries = [str(Path(python_executable).parent)]
    for tool in identity.get("tools", {}).values():
        if isinstance(tool, dict) and isinstance(tool.get("path"), str):
            path_entries.append(str(Path(tool["path"]).parent))
    path_entries = list(dict.fromkeys(path_entries))
    existing_path = environment.get("PATH", "")
    environment["PATH"] = os.pathsep.join(
        path_entries + ([existing_path] if existing_path else [])
    )
    sys_prefix = identity.get("sysPrefix")
    base_prefix = identity.get("basePrefix")
    if isinstance(sys_prefix, str) and sys_prefix and sys_prefix != base_prefix:
        environment["VIRTUAL_ENV"] = sys_prefix
    else:
        environment.pop("VIRTUAL_ENV", None)
    return environment


def _toolchain_tools_respond(identity: dict[str, Any]) -> tuple[bool, str]:
    if identity.get("family") == "rknn":
        module = identity.get("module", {})
        if isinstance(module, dict) and module.get("name") == "rknn.api":
            return True, ""
        return False, "admitted RKNN toolchain does not expose the rknn.api module"
    failures = []
    for key in ("modelTransform", "modelDeploy"):
        tool = identity.get("tools", {}).get(key, {})
        path = tool.get("path")
        if not isinstance(path, str) or not path:
            failures.append(f"{key} has no executable path")
            continue
        invocation = tool.get("invocation", "python")
        if identity.get("kind") == "python-package":
            command = (
                [path, "--help"]
                if invocation == "direct"
                else [str(identity["pythonExecutable"]), path, "--help"]
            )
        else:
            image_id = identity.get("image", {}).get("id")
            command = [
                "docker",
                "run",
                "--rm",
                "--network",
                "none",
                *source_toolchain.docker_arguments(identity),
                "--entrypoint",
                path if invocation == "direct" else str(identity["pythonExecutable"]),
                str(image_id),
            ]
            if invocation != "direct":
                command.append(path)
            command.append("--help")
        process = _run(
            command,
            timeout=120,
            env=toolchain_environment(identity),
        )
        output = f"{process.stdout}\n{process.stderr}".strip()
        required_flags = {"modelTransform": "--model_def", "modelDeploy": "--mlir"}
        source_help_invalid = identity.get("layout") == "source-tree" and (
            process.returncode != 0 or required_flags[key] not in output or "Traceback (" in output
        )
        if process.returncode not in (0, 1, 2) or not output or source_help_invalid:
            failures.append(
                f"{key} did not answer --help ({_first_line(process.stderr) or process.returncode})"
            )
    return not failures, "; ".join(failures)


def _version_tuple(value: str) -> tuple[int, ...]:
    match = re.match(r"^(\d+(?:\.\d+)*)", value)
    return tuple(int(part) for part in match.group(1).split(".")) if match else ()


def _version_satisfies(actual: str, requirement: str | None) -> bool:
    if requirement in (None, ""):
        return True
    expected = str(requirement).strip()
    if expected.startswith("=="):
        return actual == expected[2:]
    if expected.startswith(">="):
        actual_tuple = _version_tuple(actual)
        expected_tuple = _version_tuple(expected[2:])
        return bool(actual_tuple and expected_tuple and actual_tuple >= expected_tuple)
    return actual == expected


def _python_environment_check(
    executable: str, requirements: dict[str, str | None]
) -> tuple[bool, str]:
    if any(not re.fullmatch(r"[A-Za-z0-9_.-]+", name) for name in requirements):
        raise WorkflowError("parameters.pythonPackages contains an invalid package name")
    script = (
        "import importlib, importlib.metadata, json, sys;"
        "req=json.loads(sys.argv[1]); out={};"
        "\nfor name in req:"
        "\n importlib.import_module(name.replace('-', '_'));"
        "\n try: out[name]=importlib.metadata.version(name)"
        "\n except importlib.metadata.PackageNotFoundError: out[name]=getattr(importlib.import_module(name.replace('-', '_')), '__version__', 'unknown')"
        "\nprint(json.dumps(out, sort_keys=True))"
    )
    process = _run([executable, "-c", script, json.dumps(requirements)], timeout=20)
    if process.returncode != 0:
        return False, _first_line(process.stderr) or "required Python packages could not be imported"
    try:
        versions = json.loads(process.stdout)
    except json.JSONDecodeError:
        return False, "Python package version output was unreadable"
    mismatches = [
        f"{name}={versions.get(name, 'unknown')} (requires {requirement})"
        for name, requirement in requirements.items()
        if not _version_satisfies(str(versions.get(name, "unknown")), requirement)
    ]
    summary = ", ".join(f"{name}={versions.get(name, 'unknown')}" for name in sorted(versions))
    if mismatches:
        return False, "; ".join(mismatches)
    version = _run([executable, "--version"])
    python_version = _first_line(version.stdout) or _first_line(version.stderr)
    return True, f"{python_version}; {summary}"


def _repository_compatibility_facts(project_root: Path) -> dict[str, Any]:
    header_path = project_root / "src" / "util" / "NnBackendConstants.h"
    guide_path = project_root / "docs" / "tutorials" / "05-model-porting" / "model-porting.md"
    resource_root = project_root / "data" / "resource"
    rknn_profiles_root = project_root / "config" / "rknn" / "platforms"
    supported_chips: set[str] = set()
    platform_profile_chips: set[str] = set()
    resource_chips: set[str] = set()
    guide_text = ""
    missing_sources: list[str] = []

    if header_path.is_file():
        header = header_path.read_text(encoding="utf-8", errors="replace")
        sophon_section = header.split("#elif", 1)[0]
        declaration = re.search(
            r"kSupportedChips\[\]\s*=\s*\{(?P<values>[^}]+)\}", sophon_section
        )
        if declaration:
            supported_chips.update(
                value.upper()
                for value in re.findall(r'"([A-Za-z0-9_-]+)"', declaration.group("values"))
            )
        else:
            missing_sources.append("supported-chip constants")
    else:
        missing_sources.append("src/util/NnBackendConstants.h")

    if rknn_profiles_root.is_dir():
        for profile_path in rknn_profiles_root.glob("*.json"):
            try:
                profile = load_json(profile_path)
            except WorkflowError:
                continue
            if (
                isinstance(profile, dict)
                and profile.get("backend") == "rknn"
                and isinstance(profile.get("chip"), str)
            ):
                chip = profile["chip"].strip().upper()
                supported_chips.add(chip)
                platform_profile_chips.add(chip)
    else:
        missing_sources.append("config/rknn/platforms")

    if resource_root.is_dir():
        for config_path in resource_root.rglob("*.json"):
            try:
                config = load_json(config_path)
            except WorkflowError:
                continue
            if isinstance(config, dict) and isinstance(config.get("chip_type"), str):
                resource_chips.add(config["chip_type"].strip().upper())
    else:
        missing_sources.append("data/resource")

    if guide_path.is_file():
        guide_text = guide_path.read_text(encoding="utf-8", errors="replace").upper()
    else:
        missing_sources.append("model-porting guide")
    return {
        "supportedChips": supported_chips,
        "platformProfileChips": platform_profile_chips,
        "resourceChips": resource_chips,
        "guideText": guide_text,
        "missingSources": missing_sources,
    }


def _artifact_chip_fact(source_path: Path, params: dict[str, Any]) -> tuple[str | None, str]:
    declared = params.get("artifactChip")
    if isinstance(declared, str) and declared.strip():
        return declared.strip().upper(), "parameters.artifactChip"
    if source_path.suffix.lower() != ".nn":
        return None, ""
    config_path = source_path.parent / "config.json"
    if not config_path.is_file():
        return None, "adjacent config.json is missing"
    try:
        config = load_json(config_path)
    except WorkflowError as error:
        return None, f"adjacent config.json is unreadable ({error})"
    chip = config.get("chip_type") if isinstance(config, dict) else None
    if not isinstance(chip, str) or not chip.strip():
        return None, "adjacent config.json has no chip_type"
    return chip.strip().upper(), "adjacent config.json chip_type"


def compatibility_matrix_check(
    source_path: Path,
    target_chip: str,
    params: dict[str, Any],
    project_root: Path = PROJECT_ROOT,
) -> dict[str, Any]:
    target = target_chip.strip().upper()
    facts = _repository_compatibility_facts(project_root)
    suffix = source_path.suffix.lower()
    artifact_type = "model.nn" if source_path.name.lower() == "model.nn" else suffix or "unknown"
    artifact_chip, artifact_source = _artifact_chip_fact(source_path, params)
    declared_toolchain_chip = params.get("toolchainChip")
    toolchain_chip = (
        declared_toolchain_chip.strip().upper()
        if isinstance(declared_toolchain_chip, str) and declared_toolchain_chip.strip()
        else target
    )

    mismatches = []
    if toolchain_chip != target:
        mismatches.append(f"targetChip={target} conflicts with toolchainChip={toolchain_chip}")
    if artifact_chip and artifact_chip != target:
        mismatches.append(
            f"targetChip={target} conflicts with artifact chip={artifact_chip} from {artifact_source}"
        )
    if artifact_chip and artifact_chip != toolchain_chip:
        mismatches.append(
            f"artifact chip={artifact_chip} conflicts with toolchainChip={toolchain_chip}"
        )
    if mismatches:
        return _check(
            "compatibility-matrix",
            "FAIL",
            "; ".join(dict.fromkeys(mismatches)) + ".",
            remediation=(
                "Correct the task contract or select material and a toolchain that describe the same "
                "target chip, then rerun doctor."
            ),
            outcome="repairable",
            owner="task-contract",
        )

    unknown = list(facts["missingSources"])
    if target not in facts["supportedChips"]:
        unknown.append(f"code support for targetChip={target}")
    if _conversion_toolchain_family(target) == "rknn":
        if target not in facts["platformProfileChips"]:
            unknown.append(f"an RKNN platform profile for targetChip={target}")
    elif target not in facts["resourceChips"]:
        unknown.append(f"a repository resource example for targetChip={target}")
    guide_text = facts["guideText"]
    required_guide_tokens = (
        (".ONNX", ".RKNN")
        if _conversion_toolchain_family(target) == "rknn"
        else (".ONNX", ".BMODEL", "MODEL.NN")
    )
    if target not in guide_text or not all(token in guide_text for token in required_guide_tokens):
        unknown.append(f"model-porting documentation for targetChip={target} and artifact types")
    if suffix == ".bmodel" and not artifact_chip:
        unknown.append("inspected chip metadata for the .bmodel artifact")
    elif suffix == ".nn" and not artifact_chip:
        unknown.append(artifact_source or "chip_type for the model.nn artifact")
    elif suffix not in {".onnx", ".bmodel", ".nn", ".rknn"}:
        unknown.append(f"repository mapping for artifact type {artifact_type}")
    if unknown:
        return _check(
            "compatibility-matrix",
            "UNVERIFIED",
            (
                f"No mismatch was inferred for targetChip={target}, artifact={artifact_type}, and "
                f"toolchainChip={toolchain_chip}, but these facts are missing: "
                + "; ".join(dict.fromkeys(unknown))
                + "."
            ),
            remediation=(
                "Continue only as an exploratory path and obtain repository-backed or measured chip "
                "metadata before publishing a compatibility claim."
            ),
        )
    artifact_detail = (
        "chip-neutral ONNX source"
        if suffix == ".onnx"
        else f"{artifact_type} artifactChip={artifact_chip}"
    )
    return _check(
        "compatibility-matrix",
        "PASS",
        (
            f"targetChip={target} matches the {artifact_detail}, toolchainChip={toolchain_chip}, "
            "and repository code, platform/resource mapping, and model-porting facts."
        ),
    )


def task_environment_report(
    task: str,
    contract_path: Path,
    run_dir: Path,
    contract: dict[str, Any],
    project_root: Path = PROJECT_ROOT,
) -> dict[str, Any]:
    if task != contract["task"]:
        raise WorkflowError("--task must match task-contract.json")
    route_assessment: dict[str, Any] | None = None
    route_assessment_sha256: str | None = None
    if task == "model-conversion":
        route_assessment = read_route_assessment(contract_path, run_dir, contract)
        route_assessment_sha256 = sha256_file(run_dir / "route-assessment.json")
    inventory = host_inventory(project_root)
    params = contract.get("parameters", {})
    checks: list[dict[str, Any]] = []
    toolchain_family = (
        _conversion_toolchain_family(str(params.get("targetChip", "")))
        if task == "model-conversion"
        else None
    )
    toolchain_label = _conversion_toolchain_label(toolchain_family)

    architecture = inventory["host"]["architecture"].lower()
    host_os = str(inventory["host"]["os"])
    allowed_architectures = [str(value).lower() for value in params.get("hostArchitectures", [])]
    if task == "model-conversion" and not allowed_architectures:
        allowed_architectures = ["x86_64", "amd64"]
    memory_available = inventory["host"]["memoryBytes"]["available"]
    disk_free = inventory["host"]["diskBytes"]["free"]
    minimum_memory = int(params.get("minimumMemoryGiB", 0) * 1024**3)
    minimum_disk = int(params.get("minimumDiskGiB", 0) * 1024**3)
    resource_failures = []
    if allowed_architectures and architecture not in allowed_architectures:
        resource_failures.append(
            f"host architecture {architecture} is not one of {', '.join(allowed_architectures)}"
        )
    if minimum_memory and (memory_available is None or memory_available < minimum_memory):
        resource_failures.append("available memory is below the task contract requirement")
    if minimum_disk and disk_free < minimum_disk:
        resource_failures.append("free disk is below the task contract requirement")
    if task == "model-conversion" and host_os != "Linux":
        checks.append(
            _check(
                "C0",
                "FAIL",
                f"The current host is {host_os}; the admitted {toolchain_label} conversion path executes on Linux x86_64.",
                remediation=(
                    "Keep this machine as the orchestration client and run assessment/doctor in an "
                    "isolated Linux x86_64 development environment. Windows support elsewhere in "
                    "CosmoEdge is unchanged."
                ),
                outcome="needs_environment",
                owner="development-environment",
            )
        )
    if resource_failures:
        checks.append(
            _check(
                "C1",
                "FAIL",
                "; ".join(resource_failures),
                remediation="Use a development machine that satisfies the recorded architecture and resource requirements.",
                outcome="needs_environment",
            )
        )
    else:
        checks.append(_check("C1", "PASS", "Host architecture and task-recorded resource requirements match."))

    repository = inventory["repository"]
    if repository["commit"] and os.access(project_root, os.R_OK | os.W_OK):
        detail = (
            f"Repository identity frozen at {repository['commit']}; "
            f"tracked change count is {repository['trackedChanges']} and "
            f"untracked file count is {repository['untrackedFiles']}."
        )
        checks.append(_check("C2", "PASS", detail))
    else:
        checks.append(
            _check(
                "C2",
                "FAIL",
                "Repository identity or requested workspace access could not be confirmed.",
                remediation="Open a writable isolated checkout and rerun the admission check.",
                outcome="needs_environment",
            )
        )

    missing_tools = []
    for name in _required_tool_names(contract):
        if name in {"docker", "python", "python3"} and task == "model-conversion":
            continue
        if not shutil.which(name):
            missing_tools.append(name)
    if missing_tools:
        checks.append(
            _check(
                "C3",
                "FAIL",
                f"Required tools are missing: {', '.join(missing_tools)}.",
                remediation="Approve a specific dependency plan or provide a machine with these tools already installed.",
                outcome="repairable",
            )
        )
    else:
        checks.append(_check("C3", "PASS", "Task-recorded host tools are available."))

    toolchain: dict[str, Any] | None = None
    if task == "model-conversion":
        source_model = params.get("sourceModel")
        target_chip = params.get("targetChip")
        _require_nonempty_string(source_model, "parameters.sourceModel")
        _require_nonempty_string(target_chip, "parameters.targetChip")
        source_path = resolve_run_input(run_dir, source_model)
        checks.append(
            compatibility_matrix_check(source_path, target_chip, params, project_root)
        )

        toolchain_spec, specification_error = _toolchain_spec(params)
        docker_ready = False
        if toolchain_spec and toolchain_spec["kind"] == "container-image":
            docker_path = shutil.which("docker")
            if not docker_path:
                checks.append(
                    _check(
                        "C4",
                        "FAIL",
                        "The selected complete-toolchain image requires Docker, but Docker is unavailable.",
                        remediation=(
                            "Verify the selected route first, then provide Docker through the normal "
                            "IT process or select an already installed python-package toolchain."
                        ),
                        outcome="repairable",
                        owner="development-environment",
                    )
                )
            else:
                daemon = _run(
                    [docker_path, "info", "--format", "{{.ServerVersion}}"],
                    timeout=15,
                )
                if daemon.returncode == 0:
                    docker_ready = True
                    checks.append(
                        _check(
                            "C4",
                            "PASS",
                            f"Docker daemon is available ({_first_line(daemon.stdout)}).",
                        )
                    )
                else:
                    checks.append(
                        _check(
                            "C4",
                            "FAIL",
                            "Docker is installed but unavailable to the current user.",
                            remediation=(
                                "Have an authorized operator start or grant access to Docker, "
                                "then rerun doctor."
                            ),
                            outcome="repairable",
                            owner="development-environment",
                        )
                    )
        else:
            checks.append(
                _check(
                    "C4",
                    "SKIP",
                    (
                        "Docker is not required by the selected installed-Python toolchain route."
                        if toolchain_spec
                        else "Docker cannot be selected until the complete compiler toolchain is specified."
                    ),
                )
            )

        if host_os != "Linux":
            checks.append(
                _check(
                    "C5",
                    "SKIP",
                    f"{toolchain_label} capability admission is deferred to the selected Linux execution environment.",
                )
            )
        elif not toolchain_spec:
            checks.append(
                _check(
                    "C5",
                    "FAIL",
                    specification_error,
                    remediation=(
                        "Correct the repository instructions or generated task contract and validate "
                        "the complete compiler path before asking the customer to change their machine."
                    ),
                    outcome="repairable",
                    owner="repository",
                )
            )
        else:
            can_inspect = (
                toolchain_spec["kind"] in {"auto", "python-package"}
                or (toolchain_spec["kind"] == "container-image" and docker_ready)
            )
            inspected, toolchain_error = (
                inspect_toolchain(toolchain_spec)
                if can_inspect
                else (None, "the selected execution layer is not available")
            )
            if inspected:
                tools_ready, tools_error = _toolchain_tools_respond(inspected)
                if tools_ready:
                    toolchain = inspected
                    compiler_info = inspected.get("compiler", inspected.get("package", {}))
                    checks.append(
                        _check(
                            "C5",
                            "PASS",
                            (
                                f"Complete {toolchain_label} toolchain is frozen as {inspected['id']} "
                                f"({compiler_info['name']} "
                                f"{compiler_info['version']})."
                            ),
                        )
                    )
                else:
                    checks.append(
                        _check(
                            "C5",
                            "FAIL",
                            f"{toolchain_label} package exists but its conversion interface is unusable: {tools_error}.",
                            remediation=(
                                "Repair or replace the isolated compiler environment; do not treat "
                                "package files alone as READY."
                            ),
                            outcome="repairable",
                            owner="development-environment",
                        )
                    )
            else:
                official_base_only = (
                    toolchain_family == "sophon"
                    and
                    toolchain_spec["kind"] == "container-image"
                    and toolchain_spec.get("layout") != "source-tree"
                    and str(toolchain_spec.get("image", "")).startswith("sophgo/tpuc_dev:")
                )
                checks.append(
                    _check(
                        "C5",
                        "FAIL",
                        (
                            "The declared image is a base development environment, not a complete "
                            f"TPU-MLIR compiler: {toolchain_error}."
                            if official_base_only
                            else f"The declared {toolchain_label} toolchain is unavailable: {toolchain_error}."
                        ),
                        remediation=(
                            "Add and freeze the TPU-MLIR package in that environment or select an "
                            "existing isolated Python environment. Revalidate the repository "
                            "instructions before asking the customer to install anything."
                            if official_base_only
                            else "Verify the declared reference against upstream documentation, then "
                            "repair the isolated development environment only with explicit approval."
                        ),
                        outcome="repairable",
                        owner=(
                            "repository"
                            if official_base_only
                            else "development-environment"
                        ),
                    )
                )

        preflight = params.get("preflight", {})
        if not isinstance(preflight, dict):
            raise WorkflowError("parameters.preflight must be an object")
        python_name = str(
            preflight.get(
                "pythonExecutable",
                params.get("pythonExecutable", "python3"),
            )
        )
        python_path = shutil.which(python_name) if not Path(python_name).is_absolute() else python_name
        if not python_path or not Path(python_path).is_file():
            checks.append(
                _check(
                    "C6",
                    "FAIL",
                    f"Python executable is unavailable: {python_name}.",
                    remediation="Use an approved virtual environment with Python, onnx, onnxruntime, and numpy.",
                    outcome="repairable",
                    owner="development-environment",
                )
            )
        else:
            package_requirements = preflight.get(
                "pythonPackages",
                params.get(
                    "pythonPackages",
                    {"numpy": None, "onnx": None, "onnxruntime": None},
                ),
            )
            if not isinstance(package_requirements, dict) or any(
                requirement is not None and not isinstance(requirement, str)
                for requirement in package_requirements.values()
            ):
                raise WorkflowError(
                    "parameters.preflight.pythonPackages must map package names to versions or null"
                )
            imports_ok, detail = _python_environment_check(python_path, package_requirements)
            if imports_ok:
                checks.append(_check("C6", "PASS", f"ONNX preflight runtime is available ({detail})."))
            else:
                checks.append(
                    _check(
                        "C6",
                        "FAIL",
                        f"ONNX preflight dependencies are incomplete: {detail}.",
                        remediation="Approve an isolated venv or provide one with numpy, onnx, and onnxruntime.",
                        outcome="repairable",
                        owner="development-environment",
                    )
                )
    else:
        checks.extend(
            [
                _check("C4", "SKIP", "Docker is not required by this task profile."),
                _check("C5", "SKIP", "No model-conversion toolchain is required."),
                _check("C6", "SKIP", "No model export or ONNX preflight is required."),
            ]
        )

    required_grants = {
        grant
        for grant, fields in {
            "environment-change": ("requiresEnvironmentChange",),
            "remote-execution": ("requiresRemoteExecution", "requiresNetwork"),
            "model-transfer": ("requiresModelTransfer",),
            "device-deployment": ("requiresDevice", "requiresDeployment"),
        }.items()
        if any(bool(params.get(field)) for field in fields)
    }
    missing_grants = sorted(required_grants - _authority_grants(contract))
    if missing_grants:
        checks.append(
            _check(
                "C7",
                "BLOCKED",
                "The route requires grants that are not recorded: " + ", ".join(missing_grants) + ".",
                remediation=(
                    "Confirm these coarse task-scoped grants together with the target and recovery "
                    "boundary; do not place credentials in the contract."
                ),
                outcome="repairable",
            )
        )
    elif required_grants:
        checks.append(_check("C7", "PASS", "Required external capability is explicitly represented in authority."))
    else:
        checks.append(_check("C7", "SKIP", "No additional coarse-grained authority is required by this route."))

    verdict = environment_verdict(checks)
    needs_input: list[dict[str, Any]] = []
    if missing_grants:
        needs_input.append(
            _needs_input(
                "required-authority",
                "请确认是否仅为本次隔离开发任务授予：" + "、".join(missing_grants) + "。",
                "这些权限不会从工作区读写权限自动继承。",
                category="authority",
                required_before="external-action",
            )
        )
    repair_requires_change = any(
        item.get("_outcome") == "repairable"
        and item.get("owner") == "development-environment"
        for item in checks
    )
    if repair_requires_change and "environment-change" not in _authority_grants(contract):
        needs_input.append(
            _needs_input(
                "environment-change-plan",
                "当前路径需要改变隔离开发环境。请先审阅智能体给出的依赖、影响和回退方案，再决定是否授权环境变更。",
                "只读环境检查不包含安装依赖、拉取镜像、启动服务或提权。",
                category="authority",
                required_before="environment-change",
            )
        )
    public_checks = [{key: value for key, value in item.items() if key != "_outcome"} for item in checks]
    return {
        "schemaVersion": "1.0",
        "commit": inventory["repository"]["commit"],
        "tree": inventory["repository"]["tree"],
        "task": task,
        "runId": contract["runId"],
        "contractSha256": sha256_file(contract_path),
        "routeAssessment": "route-assessment.json" if route_assessment else None,
        "routeAssessmentSha256": route_assessment_sha256,
        "authority": redact_data(contract["authority"]),
        "host": inventory["host"],
        "repository": inventory["repository"],
        "checks": public_checks,
        "toolchain": toolchain,
        "needsInput": needs_input,
        "environmentVerdict": verdict,
    }


def atomic_write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        fchmod = getattr(os, "fchmod", None)
        if callable(fchmod):
            fchmod(descriptor, 0o600)
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(data, stream, ensure_ascii=False, indent=2)
            stream.write("\n")
        temporary.replace(path)
        path.chmod(0o600)
    finally:
        if temporary.exists():
            temporary.unlink()


def _utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def _new_run_id(task: str) -> str:
    prefix = re.sub(r"[^A-Za-z0-9._-]+", "-", task).strip(".-_") or "task"
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    return f"{prefix[:32]}-{timestamp}-{token_hex(4)}"


def _safe_record_text(value: str, field: str) -> str:
    _require_nonempty_string(value, field)
    return redact_text(value.strip())


def _authority_record_payload(
    grants: Iterable[str],
    *,
    scope: str,
    target_reference: str,
    impact: str,
    recovery: str,
) -> dict[str, Any]:
    return {
        "recordedAt": _utc_now(),
        "confirmedByUser": True,
        "grants": sorted({str(item) for item in grants}),
        "scope": _safe_record_text(scope, "scope"),
        "targetReference": _safe_record_text(target_reference, "target reference"),
        "impact": _safe_record_text(impact, "impact"),
        "recovery": _safe_record_text(recovery, "recovery"),
        "credentialMaterialStored": False,
    }


def create_task_run(
    *,
    task: str,
    objective: str,
    deliverables: Iterable[str] = (),
    materials: Iterable[str | os.PathLike[str]] = (),
    target_chip: str | None = None,
    remote_linux: bool = False,
    user_requested_remote_access: bool = False,
    run_id: str | None = None,
    project_root: Path = PROJECT_ROOT,
) -> tuple[Path, Path, dict[str, Any], dict[str, Any]]:
    """Create a private agent-owned task record from ordinary user intent."""
    task = _safe_record_text(task, "task")
    objective = _safe_record_text(objective, "objective")
    selected_run_id = run_id or _new_run_id(task)
    if not RUN_ID_PATTERN.fullmatch(selected_run_id):
        raise WorkflowError("runId must contain only letters, numbers, dot, underscore, or dash")
    if target_chip and task != "model-conversion":
        raise WorkflowError("--target-chip is only valid with task=model-conversion")
    if user_requested_remote_access and not remote_linux:
        raise WorkflowError("explicit remote access requires --remote-linux")

    requested_deliverables = [
        _safe_record_text(str(item), "deliverable") for item in deliverables
    ]
    if not requested_deliverables:
        requested_deliverables = (
            ["模型转换产物与可复核的开发验证证据"]
            if task == "model-conversion"
            else ["用户请求的交付物与可复核验证证据"]
        )

    sources: list[Path] = []
    names: set[str] = set()
    for raw_material in materials:
        candidate = Path(raw_material).expanduser()
        try:
            source = candidate.resolve(strict=True)
        except FileNotFoundError as error:
            raise WorkflowError(f"material does not exist: {candidate}") from error
        if not source.is_file():
            raise WorkflowError(f"material must be a file: {candidate}")
        if source.name in names:
            raise WorkflowError(f"material file names must be unique: {source.name}")
        names.add(source.name)
        sources.append(source)

    root = project_root.resolve()
    runs_root = root / "output" / "agent-runs"
    runs_root.mkdir(parents=True, exist_ok=True, mode=0o700)
    run_dir = runs_root / selected_run_id
    try:
        run_dir.mkdir(mode=0o700)
    except FileExistsError as error:
        raise WorkflowError(f"run already exists and will not be overwritten: {selected_run_id}") from error
    run_dir.chmod(0o700)

    material_paths: list[str] = []
    if sources:
        inputs_dir = run_dir / "inputs"
        inputs_dir.mkdir(mode=0o700)
        for source in sources:
            destination = inputs_dir / source.name
            shutil.copyfile(source, destination)
            destination.chmod(0o600)
            material_paths.append(destination.relative_to(run_dir).as_posix())

    parameters: dict[str, Any] = {}
    if task == "model-conversion":
        model_material_paths = [
            path
            for path in material_paths
            if Path(path).suffix.lower() in {".onnx", ".pt", ".pth", ".mlir", ".bmodel", ".nn"}
        ]
        if len(model_material_paths) == 1:
            parameters["sourceModel"] = model_material_paths[0]
    if target_chip:
        parameters["targetChip"] = target_chip.strip().lower()
    if remote_linux:
        parameters["developmentEnvironment"] = {
            "os": "linux",
            "architecture": "x86_64",
            "reference": "customer-provided isolated Linux development environment",
        }
        parameters["requiresRemoteExecution"] = True
        parameters["requiresModelTransfer"] = bool(material_paths)

    authority: dict[str, Any] = {
        "workspace": "current isolated checkout and this private run directory",
        "grants": [],
    }
    if user_requested_remote_access:
        initial_grants = {"remote-execution"}
        if material_paths:
            initial_grants.add("model-transfer")
        authority["grants"] = sorted(initial_grants)
        authority["grantRecords"] = [
            _authority_record_payload(
                initial_grants,
                scope="current isolated development task only",
                target_reference="user-provided isolated Linux development environment",
                impact="remote inspection and task-scoped work requested by the user",
                recovery="close the remote session and remove task-scoped temporary files",
            )
        ]

    contract: dict[str, Any] = {
        "schemaVersion": "1.0",
        "runId": selected_run_id,
        "task": task,
        "userObjective": objective,
        "expectedDeliverables": requested_deliverables,
        "allowedChanges": [f"output/agent-runs/{selected_run_id}/"],
        "requiredCapabilities": [],
        "acceptance": {},
        "authority": authority,
        "parameters": parameters,
    }
    validate_contract(contract)
    contract_path = run_dir / "task-contract.json"
    atomic_write_json(contract_path, contract)
    assessment = assess_task_report(contract_path, run_dir, contract, project_root=root)
    atomic_write_json(run_dir / "route-assessment.json", assessment)
    return run_dir, contract_path, contract, assessment


def record_authority_grants(
    contract_path: Path,
    run_dir: Path,
    contract: dict[str, Any],
    *,
    grants: Iterable[str],
    confirmed_by_user: bool,
    scope: str = "current isolated development task only",
    target_reference: str = "task-scoped isolated target",
    impact: str = "limited to the explicitly granted capability",
    recovery: str = "stop the action and restore or recreate the isolated development environment",
) -> dict[str, Any]:
    """Record coarse authority after an explicit user confirmation, without credentials."""
    if not confirmed_by_user:
        raise WorkflowError("authority cannot be recorded without explicit user confirmation")
    requested = {str(item) for item in grants}
    if not requested:
        raise WorkflowError("at least one authority grant is required")
    unknown = sorted(requested - AUTHORITY_GRANTS)
    if unknown:
        raise WorkflowError(f"unknown authority grants: {', '.join(unknown)}")
    record = _authority_record_payload(
        requested,
        scope=scope,
        target_reference=target_reference,
        impact=impact,
        recovery=recovery,
    )
    authority = contract["authority"]
    authority["grants"] = sorted(_authority_grants(contract) | requested)
    records = authority.setdefault("grantRecords", [])
    if not isinstance(records, list):
        raise WorkflowError("authority.grantRecords must be an array when present")
    records.append(record)
    validate_contract(contract)
    atomic_write_json(contract_path, contract)
    return record


def _print_report(report: dict[str, Any], output_format: str) -> None:
    if output_format == "json":
        print(json.dumps(report, ensure_ascii=False, indent=2))
        return
    if report.get("mode") == "baseline":
        host = report["host"]
        repository = report["repository"]
        print(f"Repository: {repository.get('commit') or 'UNVERIFIED'}")
        print(f"Host: {host['os']} {host['architecture']}")
        for name, item in report["tools"].items():
            state = "PASS" if item["available"] else "UNVERIFIED"
            print(f"[{state}] {name}: {item.get('version') or 'not found'}")
        print("Task-specific readiness is not evaluated in baseline mode.")
        return
    if report.get("mode") == "assessment":
        print(f"Recommended route: {report.get('recommendedRoute') or 'UNRESOLVED'}")
        for item in report.get("needsInput", []):
            print(f"[NEEDS_INPUT] {item['question']}")
        print(f"Route verdict: {report['routeVerdict']}")
        return
    for item in report["checks"]:
        print(f"[{item['status']}] {item['id']} {item['detail']}")
        if item.get("remediation"):
            print(f"  Remediation: {item['remediation']}")
    print(f"Environment verdict: {report['environmentVerdict']}")


def doctor_main(arguments: list[str]) -> int:
    parser = argparse.ArgumentParser(
        prog="doctor.sh",
        description="Read-only CosmoEdge development-environment inventory and task admission.",
    )
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--baseline", action="store_true")
    mode.add_argument("--task")
    parser.add_argument("--contract")
    parser.add_argument("--format", choices=("text", "json"), default="text")
    options = parser.parse_args(arguments)

    if options.baseline:
        if options.contract:
            parser.error("--contract is only valid with --task")
        _print_report(baseline_report(), options.format)
        return 0
    if not options.contract:
        parser.error("--contract is required with --task")
    contract_path, run_dir, contract = resolve_contract_context(options.contract)
    report = task_environment_report(options.task, contract_path, run_dir, contract)
    atomic_write_json(run_dir / "environment-report.json", report)
    _print_report(report, options.format)
    return 0 if report["environmentVerdict"] == "READY" else 1


def assess_main(arguments: list[str]) -> int:
    parser = argparse.ArgumentParser(
        prog="assess.sh",
        description="Read-only intent, material, route, and authority assessment.",
    )
    parser.add_argument("--contract", required=True)
    parser.add_argument("--format", choices=("text", "json"), default="text")
    options = parser.parse_args(arguments)
    contract_path, run_dir, contract = resolve_contract_context(options.contract)
    report = assess_task_report(contract_path, run_dir, contract)
    atomic_write_json(run_dir / "route-assessment.json", report)
    _print_report(report, options.format)
    return 0 if report["routeVerdict"] == "READY" else 1


def start_main(arguments: list[str]) -> int:
    parser = argparse.ArgumentParser(
        prog="start.sh",
        description="Create a private task run from ordinary user intent and assess its route.",
    )
    parser.add_argument("--task", default="model-conversion")
    parser.add_argument("--objective", required=True)
    parser.add_argument("--deliverable", action="append", default=[])
    parser.add_argument("--material", action="append", default=[])
    parser.add_argument("--target-chip")
    parser.add_argument("--remote-linux", action="store_true")
    parser.add_argument("--user-requested-remote-access", action="store_true")
    parser.add_argument("--run-id")
    parser.add_argument("--format", choices=("text", "json"), default="text")
    options = parser.parse_args(arguments)
    run_dir, contract_path, contract, assessment = create_task_run(
        task=options.task,
        objective=options.objective,
        deliverables=options.deliverable,
        materials=options.material,
        target_chip=options.target_chip,
        remote_linux=options.remote_linux,
        user_requested_remote_access=options.user_requested_remote_access,
        run_id=options.run_id,
    )
    result = {
        "runDirectory": run_dir.relative_to(PROJECT_ROOT).as_posix(),
        "contract": contract_path.relative_to(PROJECT_ROOT).as_posix(),
        "routeAssessment": (run_dir / "route-assessment.json").relative_to(PROJECT_ROOT).as_posix(),
        "routeVerdict": assessment["routeVerdict"],
        "needsInput": assessment["needsInput"],
        "authorityGrants": sorted(_authority_grants(contract)),
        "remoteConnectionAllowed": "remote-execution" in _authority_grants(contract),
    }
    if options.format == "json":
        print(json.dumps(result, ensure_ascii=False, indent=2))
    else:
        print(f"Private run: {result['runDirectory']}")
        print(f"Route verdict: {result['routeVerdict']}")
        for item in result["needsInput"]:
            print(f"[NEEDS_INPUT] {item['question']}")
        if result["remoteConnectionAllowed"]:
            print("Next: connect to the declared development environment for read-only inspection.")
        else:
            print("Next: resolve needsInput, record any explicit authority, then rerun assess before doctor.")
    return 0


def authorize_main(arguments: list[str]) -> int:
    parser = argparse.ArgumentParser(
        prog="authorize.sh",
        description="Record task-scoped coarse authority after explicit user confirmation.",
    )
    parser.add_argument("--contract", required=True)
    parser.add_argument("--grant", action="append", choices=sorted(AUTHORITY_GRANTS), required=True)
    parser.add_argument("--confirmed-by-user", action="store_true")
    parser.add_argument("--scope", default="current isolated development task only")
    parser.add_argument("--target-reference", default="task-scoped isolated target")
    parser.add_argument("--impact", default="limited to the explicitly granted capability")
    parser.add_argument(
        "--recovery",
        default="stop the action and restore or recreate the isolated development environment",
    )
    options = parser.parse_args(arguments)
    contract_path, run_dir, contract = resolve_contract_context(options.contract)
    record = record_authority_grants(
        contract_path,
        run_dir,
        contract,
        grants=options.grant,
        confirmed_by_user=options.confirmed_by_user,
        scope=options.scope,
        target_reference=options.target_reference,
        impact=options.impact,
        recovery=options.recovery,
    )
    print("Recorded grants: " + ", ".join(record["grants"]))
    print("Existing assessment and environment reports are now stale.")
    print("Next: rerun assess, then doctor on the actual execution machine.")
    return 0


def main(arguments: list[str] | None = None) -> int:
    args = list(sys.argv[1:] if arguments is None else arguments)
    if not args:
        print("usage: agent_workflow.py start|assess|authorize|doctor ...", file=sys.stderr)
        return 2
    command = args.pop(0)
    try:
        if command == "start":
            return start_main(args)
        if command == "assess":
            return assess_main(args)
        if command == "authorize":
            return authorize_main(args)
        if command == "doctor":
            return doctor_main(args)
        raise WorkflowError(f"unknown command: {command}")
    except WorkflowError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    os.umask(0o077)
    raise SystemExit(main())
