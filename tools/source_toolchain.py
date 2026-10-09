"""Admission for existing TPU-MLIR source builds mounted into a frozen image.

This layout does not install packages, run envsetup.sh, or manufacture wheel
metadata. Compiler source/runtime contents and the execution image are separate
parts of the admitted identity.
"""

from __future__ import annotations

import os
from pathlib import Path, PurePosixPath
from typing import Any


ENVIRONMENT_KEYS = {
    "PATH", "PYTHONPATH", "LD_LIBRARY_PATH", "TPUC_ROOT", "OMP_NUM_THREADS",
    "USING_CMODEL", "CMODEL_LD_LIBRARY_PATH", "CUSTOM_LAYER_PATH",
}


def normalize(raw: dict[str, Any]) -> dict[str, Any]:
    root = raw.get("sourceRoot")
    mount = raw.get("sourceMount", "/opt/tpu-mlir")
    for name, value in (("sourceRoot", root), ("sourceMount", mount)):
        if (not isinstance(value, str) or not value.startswith("/")
                or any(char in value for char in ("\0", "\n", "\r", ","))
                or ".." in PurePosixPath(value).parts):
            raise ValueError(f"source-tree {name} must be an absolute path without traversal or commas")
    root = str(PurePosixPath(root))
    mount = str(PurePosixPath(mount))
    if not mount.startswith("/opt/"):
        raise ValueError("source-tree sourceMount must be a dedicated directory under /opt")
    environment = {
        "TPUC_ROOT": f"{mount}/install",
        "PATH": f"{mount}/install/bin:{mount}/python/tools:{mount}/python/utils:"
                "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
        "PYTHONPATH": f"{mount}/install/python:{mount}/python",
        "LD_LIBRARY_PATH": f"{mount}/install/lib:{mount}/capi/lib",
    }
    declared = raw.get("environment", {})
    if not isinstance(declared, dict):
        raise ValueError("source-tree environment must be an object")
    for key, value in declared.items():
        if (key not in ENVIRONMENT_KEYS or not isinstance(value, str) or not value
                or any(char in value for char in ("\0", "\n", "\r", "$", "`"))):
            raise ValueError("source-tree environment requires supported keys and literal string values")
        environment[key] = value
    # Imports must not mutate an existing source checkout or its symlink targets.
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    return {"sourceRoot": root, "sourceMount": mount, "environment": environment}


def docker_arguments(identity: dict[str, Any]) -> list[str]:
    if identity.get("layout") != "source-tree":
        return []
    source = identity["sourceTree"]
    arguments = ["--mount", f"type=bind,src={source['sourceRoot']},"
                 f"dst={source['sourceMount']},readonly"]
    for name, value in sorted(source["environment"].items()):
        arguments.extend(["--env", f"{name}={value}"])
    return arguments


def validate_host_root(specification: dict[str, Any]) -> None:
    root = Path(specification["sourceTree"]["sourceRoot"])
    if not root.is_dir() or not os.access(root, os.R_OK | os.X_OK):
        raise ValueError("declared source-tree root is not an existing readable directory")


# Executed by the selected image's Python. No shell startup/environment script
# is sourced. File hashes stream in bounded chunks, and duplicate symlink
# targets are hashed once per probe. Build caches, regression datasets and
# model weights outside the execution/source roots are not traversed.
PROBE_SCRIPT = r'''
import hashlib
import contextlib
import importlib
import json
import os
import pathlib
import shutil
import subprocess
import sys

root = pathlib.Path(sys.argv[1]).resolve(strict=True)
declared_tools = json.loads(sys.argv[2])
module_name = sys.argv[3]
hashes = {}

def digest_file(path):
    resolved = path.resolve(strict=True)
    key = str(resolved)
    if key not in hashes:
        digest = hashlib.sha256()
        with resolved.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
        hashes[key] = digest.hexdigest()
    return hashes[key]

def inside(path):
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False

def tool(key, relative, required=True):
    raw = declared_tools.get(key) or str(root / relative)
    path = pathlib.Path(raw)
    if not path.is_absolute():
        resolved = shutil.which(raw)
        path = pathlib.Path(resolved) if resolved else path
    if not path.is_file():
        if required or key in declared_tools:
            raise RuntimeError("source-tree tool is unavailable: " + key)
        return None
    path = path.resolve(strict=True)
    if not inside(path):
        raise RuntimeError("source-tree tool resolves outside the mounted compiler: " + key)
    with path.open("rb") as stream:
        head = stream.read(256)
    first = head.splitlines()[0].lower() if head else b""
    return {"path": str(path), "sha256": digest_file(path),
            "resolution": "declared" if key in declared_tools else "source-layout",
            "invocation": "python" if path.suffix == ".py" or b"python" in first else "direct"}

tools = {
    "modelTransform": tool("modelTransform", "python/tools/model_transform.py"),
    "modelDeploy": tool("modelDeploy", "python/tools/model_deploy.py"),
    "modelTool": tool("modelTool", "install/bin/model_tool", required=False),
}
compiler = tool("compiler", "install/bin/tpuc-opt")
if shutil.which("tpuc-opt") is None or pathlib.Path(shutil.which("tpuc-opt")).resolve() != pathlib.Path(compiler["path"]):
    raise RuntimeError("PATH does not resolve tpuc-opt to the mounted compiler")
version_command = subprocess.run([compiler["path"], "--version"], capture_output=True, text=True, timeout=30)
if version_command.returncode != 0 or not version_command.stdout.strip():
    raise RuntimeError("source-tree tpuc-opt does not report its version")
with contextlib.redirect_stdout(sys.stderr):
    module = importlib.import_module(module_name)
module_path = pathlib.Path(module.__file__).resolve(strict=True)
if not inside(module_path):
    raise RuntimeError("source-tree Python module resolves outside the mounted compiler")
version = getattr(module, "__version__", None)
if not isinstance(version, str) or not version:
    version = version_command.stdout.strip()

required_roots = ("python", "install/bin", "install/lib", "install/python")
for relative in required_roots:
    if not (root / relative).is_dir():
        raise RuntimeError("source-tree runtime directory is missing: " + relative)
# Include all executable Python and installed artifacts, plus compiler sources.
# The large build/regression/third_party caches are not runtime inputs.
roots = (*required_roots, "capi", "include", "lib", "tools", "bindings",
         "third_party/customlayer", "CMakeLists.txt", "envsetup.sh", "build.sh")
files = {}
active = set()

def visit(path):
    relative = str(path.relative_to(root))
    if path.name == "__pycache__" or path.suffix in (".pyc", ".pyo"):
        return
    if path.is_symlink():
        target = os.readlink(path)
        if not path.exists():
            files[relative] = {"link": target, "missing": True}
            return
        resolved = path.resolve(strict=True)
        if path.is_dir() and not inside(resolved):
            raise RuntimeError("source-tree directory symlink leaves compiler root: " + relative)
        files[relative] = {"link": target, "resolved": str(resolved)}
    if path.is_dir():
        key = str(path.resolve(strict=True))
        if key in active:
            raise RuntimeError("source-tree contains a directory symlink cycle: " + relative)
        active.add(key)
        for child in sorted(path.iterdir()):
            visit(child)
        active.remove(key)
    elif path.is_file():
        files.setdefault(relative, {}).update({"sha256": digest_file(path), "sizeBytes": path.stat().st_size})
    elif path.exists():
        raise RuntimeError("unsupported special file in compiler runtime: " + relative)

for relative in roots:
    path = root / relative
    if path.exists() or path.is_symlink():
        visit(path)
manifest = json.dumps(files, sort_keys=True, separators=(",", ":"))
source = {"roots": list(roots), "fileCount": len(files),
          "contentSha256": hashlib.sha256(manifest.encode()).hexdigest(), "files": files}
git = shutil.which("git")
if git and (root / ".git").exists():
    def git_read(arguments):
        result = subprocess.run([git, "-c", "safe.directory=" + str(root), "-C", str(root), *arguments],
                                capture_output=True, timeout=30)
        if result.returncode != 0:
            raise RuntimeError("source-tree Git provenance cannot be read")
        return result.stdout
    source["git"] = {"commit": git_read(["rev-parse", "HEAD"]).decode().strip(),
                     "tree": git_read(["rev-parse", "HEAD^{tree}"]).decode().strip(),
                     "statusSha256": hashlib.sha256(git_read(["status", "--porcelain=v1", "-z", "--untracked-files=no"])).hexdigest()}

print(json.dumps({"family": "sophon", "layout": "source-tree",
    "pythonExecutable": str(pathlib.Path(sys.executable).absolute()),
    "pythonVersion": sys.version.split()[0],
    "sysPrefix": str(pathlib.Path(sys.prefix).absolute()),
    "basePrefix": str(pathlib.Path(getattr(sys, "base_prefix", sys.prefix)).absolute()),
    "compiler": {"name": "tpu-mlir", "version": version, "executable": compiler,
                 "versionOutput": version_command.stdout.strip(), "source": source},
    "module": {"name": module_name, "path": str(module_path), "sha256": digest_file(module_path)},
    "tools": tools}, sort_keys=True))
'''
