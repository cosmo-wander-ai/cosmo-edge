"""Verify Cppcheck's standalone-header member diagnostics against source uses."""

import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from functools import lru_cache
import xml.etree.ElementTree as ET


MEMBER_ID = "unusedStructMember"
DIAGNOSTICS = 42
GROUPS = ("src", "test")
EVIDENCE_REQUEST = "COSMO_CPPCHECK_MEMBER_REQUEST"
RECORD_SCOPES = {"Class", "Struct", "Union"}
CPU_DEFINES = (
    "COSMO_NN_USE_CPU_BACKEND", "COSMO_NN_USE_HOST_BACKEND", "COSMO_NN_USE_ONNX_BACKEND",
    "COSMO_MEDIA_USE_CPU_BACKEND", "__x86_64__",
)
SOPHON_DEFINES = (
    "COSMO_NN_USE_SOPHON_BACKEND", "COSMO_MEDIA_USE_SOPHON_BACKEND", "__aarch64__",
)
ROCKCHIP_DEFINES = (
    "COSMO_NN_USE_RKNN_BACKEND", "COSMO_NN_USE_HOST_BACKEND", "COSMO_NN_USE_RAW_MODEL_BACKEND",
    "COSMO_MEDIA_USE_ROCKCHIP_BACKEND", "__aarch64__",
)


def source_paths(root):
    """Resolve analyzer paths without admitting files outside the source tree."""
    @lru_cache(maxsize=None)
    def relative(filename):
        if filename:
            path = (root / filename).resolve()
            if path.is_relative_to(root):
                path = path.relative_to(root)
                if path.parts and path.parts[0] in GROUPS:
                    return path.as_posix()
        return ""

    return relative


def header_member(error, relative):
    location = error.find("location")
    if error.get("id") != MEMBER_ID or location is None:
        return None
    symbol = error.findtext("symbol")
    if symbol is None:
        # Cppcheck 2.13 drops the symbol element when a worker sends its report.
        match = re.fullmatch(r"(?:struct|class|union) member '(.+::[^']+)' is never used\.",
                             error.get("msg", ""))
        symbol = match[1] if match else ""
    file = relative(location.get("file"))
    if ("::" in symbol and file.endswith(".h") and location.get("line", "0") != "0"
            and location.get("column", "0") != "0"):
        return file, location.get("line"), location.get("column"), symbol
    return None


def dump_references(dump, root, members):
    """Resolve token -> variable -> declaration within each preprocessing configuration."""
    relative = source_paths(root)
    declarations = {}
    wanted = set()
    for file, line, column, symbol in members:
        name = symbol.rsplit("::", 1)[1]
        declarations.setdefault((file, line, column, name), []).append((file, line, column, symbol))
        wanted.add(name)

    found = {}
    tokens = {}
    uses = {}
    variables = {}
    scopes = {}
    for _, node in ET.iterparse(dump):
        tag = node.tag
        attrs = node.attrib
        if tag == "token" and attrs.get("str") in wanted and attrs.get("variable"):
            path = relative(attrs.get("file"))
            if path:
                location = (path, attrs.get("linenr"), attrs.get("column"), attrs["str"])
                if location in declarations:
                    tokens[attrs["id"]] = location
                else:
                    uses.setdefault(attrs["variable"], location[:3])
        elif tag == "scope" and attrs.get("type") in RECORD_SCOPES and attrs.get("className"):
            scopes[attrs["id"]] = attrs["className"]
        elif tag == "var" and attrs.get("id") in uses:
            variables[attrs["id"]] = attrs.get("nameToken"), attrs.get("scope")
        elif tag == "dump":
            for var, use in uses.items():
                declaration, scope = variables.get(var, (None, None))
                for member in declarations.get(tokens.get(declaration), ()):
                    actual_owner = scopes.get(scope, "").rsplit("::", 1)[-1]
                    expected_owner = member[3].split("::")[-2]
                    if (actual_owner == expected_owner
                            or (re.fullmatch(r"Anonymous\d+", actual_owner)
                                and re.fullmatch(r"Anonymous\d+", expected_owner))):
                        found.setdefault(member, use)
            tokens.clear()
            uses.clear()
            variables.clear()
            scopes.clear()
        node.clear()
    return found


def collect_references(dump):
    request = json.loads(Path(os.environ[EVIDENCE_REQUEST]).read_text())
    refs = dump_references(dump, Path(request["root"]), request["members"])
    dump.with_suffix(".members.json").write_text(
        json.dumps([(field, [request["configuration"], *location]) for field, location in refs.items()])
    )
    # This per-file addon is the only consumer of the dump.
    dump.unlink()


def reports(result):
    report = ET.fromstring(result.stderr)
    errors = report.find("errors")
    if report.tag != "results" or report.get("version") != "2" or errors is None:
        raise ValueError("incomplete Cppcheck XML report")
    return list(errors)


def implementation_files(root):
    def failed(error):
        raise error

    for directory in GROUPS:
        for base, dirs, names in os.walk(root / directory, onerror=failed):
            dirs[:] = sorted(set(dirs) - {"3rd", "build", "node_modules"})
            for name in sorted(names):
                if name.endswith(".cc") and not name.startswith("catch_amalgamated."):
                    path = Path(base) / name
                    if path.is_file() and not path.is_symlink():
                        yield str(path)


def index_members(root, jobs, scratch, members):
    request = scratch / "members.json"
    addon = scratch / "addon.json"
    addon.write_text(json.dumps({"script": str(Path(__file__).resolve())}))
    env = dict(os.environ, **{EVIDENCE_REQUEST: str(request)})

    sources = list(implementation_files(root))
    tests = [path for path in sources if Path(path).is_relative_to(root / "test")]
    sophon = [path for path in sources if Path(path).is_relative_to(root / "src") and (
        "/device/sophon/" in path or "Sophon" in Path(path).name)]
    rockchip = [path for path in sources if Path(path).is_relative_to(root / "src") and (
        "/device/rknn/" in path or "Rockchip" in Path(path).name)]
    production = sorted(set(sources) - set(tests) - set(sophon) - set(rockchip))
    platform = [path for path in production if Path(path).is_relative_to(root / "src/platform")]
    index_jobs = min(int(jobs), 12)
    found = {}
    for configuration, defines, files, test_args in (
        ("x86", CPU_DEFINES, production, []),
        ("sophon", SOPHON_DEFINES, sophon + platform, []),
        ("rockchip", ROCKCHIP_DEFINES, rockchip + platform, []),
        ("x86-tests", CPU_DEFINES, tests, [
            "-I", str(root / "test"), "-UCATCH_CONFIG_DISABLE", "-UCATCH_CONFIG_PREFIX_ALL",
            "-DCATCH_CONFIG_EXPERIMENTAL_STATIC_ANALYSIS_SUPPORT", "-D__has_cpp_attribute(x)=0",
            f"--include={root / 'tools/cppcheck/Catch2Model.h'}",
        ]),
    ):
        if not files or len(found) == len(members):
            continue
        request.write_text(json.dumps({
            "root": str(root), "configuration": configuration, "members": sorted(members - found.keys()),
        }))
        build = scratch / configuration
        build.mkdir()
        result = subprocess.run(
            ["cppcheck", "--xml", "--xml-version=2", "--quiet", "--std=c++17", "--language=c++",
             "-D__linux__", "-DCOSMO_NN_NAIVE_ENABLE", *("-D" + name for name in defines),
             "-I", str(root / "src"), f"-j{index_jobs}",
             f"--addon={addon}", f"--addon-python={sys.executable}", f"--cppcheck-build-dir={build}",
             *test_args, *files], cwd=root, env=env, capture_output=True,
        )
        if result.returncode:
            raise ValueError(result.stderr.decode(errors="replace") or "Cppcheck member indexing failed")
        if any(error.get("id") == "internalError" for error in reports(result)):
            raise ValueError(result.stderr.decode(errors="replace"))
        for file in sorted(build.glob("*.members.json")):
            for field, use in json.loads(file.read_text()):
                found.setdefault(tuple(field), use)
            file.unlink()
    return found


def print_error(error):
    location = error.find("location")
    file = location.get("file") if location is not None else "nofile"
    line = location.get("line") if location is not None else "0"
    print(f'{file}:{line}: [{error.get("severity")}] {error.get("id")}: {error.get("msg")}')


def verify(root, jobs, args):
    result = subprocess.run(
        ["cppcheck", "--xml", "--xml-version=2", f"--error-exitcode={DIAGNOSTICS}", *args],
        cwd=root, capture_output=True,
    )
    if result.stdout:
        sys.stdout.buffer.write(result.stdout)
    if result.returncode not in (0, DIAGNOSTICS):
        sys.stderr.buffer.write(result.stderr)
        return result.returncode if result.returncode > 0 else 128 - result.returncode

    output = os.environ.get("COSMO_CPPCHECK_REPORT_DIR")
    if output:
        directory = Path(output)
        (directory / "cppcheck.xml").write_bytes(result.stderr)
        (directory / "cppcheck-exit-status").write_text(f"{result.returncode}\n")

    errors = reports(result)
    if (result.returncode == DIAGNOSTICS) != bool(errors):
        raise ValueError("Cppcheck exit status does not match its diagnostic report")
    relative = source_paths(root)
    members = {field for e in errors if (field := header_member(e, relative))}
    found = {}
    if members:
        try:
            with tempfile.TemporaryDirectory(prefix="cppcheck-members-") as temp:
                found = index_members(root, jobs, Path(temp), members)
        except (OSError, ValueError, ET.ParseError):
            for error in errors:
                print_error(error)
            raise
        print(f"Header member diagnostics: {len(members)} candidates, "
              f"{len(found)} referenced, {len(members - found.keys())} unresolved.")
    if output:
        (directory / "header-member-references.json").write_text(
            json.dumps(sorted(found.items()), indent=2) + "\n"
        )
        (directory / "header-member-unresolved.json").write_text(
            json.dumps(sorted(members - found.keys()), indent=2) + "\n"
        )
    remaining = [error for error in errors if header_member(error, relative) not in found]
    for error in remaining:
        print_error(error)
    return int(bool(remaining))


def main():
    dump = next((Path(arg) for arg in sys.argv[1:] if arg.endswith(".dump")), None)
    if dump is not None and EVIDENCE_REQUEST in os.environ:
        collect_references(dump)
        return 0
    root, jobs, *args = sys.argv[1:]
    try:
        return verify(Path(root).resolve(), jobs, args)
    except (OSError, ValueError, ET.ParseError) as error:
        print(f"Cppcheck verification failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
