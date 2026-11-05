#!/usr/bin/env python3
"""Audit an unpacked Linux package without executing it or consulting host libraries.

PASS covers ELF architecture, numeric GLIBC requirements, declared NEEDED closure
and GNU symbol-version requirements under the reported loader-search contract.
It does not prove individual symbol/struct ABI, dlopen plugins, kernel UAPI or
runtime behavior. A target sysroot is needed to verify system dependencies.
Exit codes: 0 PASS, 1 FAIL, 2 UNVERIFIED (or invalid invocation).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
from typing import Any


ARCHITECTURES = {"aarch64": (183, "aarch64-linux-gnu"), "x86_64": (62, "x86_64-linux-gnu")}


class ElfError(ValueError):
    pass


def version_tuple(value: str) -> tuple[int, ...]:
    if not re.fullmatch(r"[0-9]+(?:\.[0-9]+)+", value):
        raise ValueError(f"invalid numeric version: {value}")
    parts = [int(part) for part in value.split(".")]
    while len(parts) > 1 and parts[-1] == 0:
        parts.pop()
    return tuple(parts)


def read_elf(path: Path) -> dict[str, Any]:
    """Read little-endian ELF64 metadata; unsupported/truncated ELF fails closed."""
    data = path.read_bytes()

    def unpack(fmt: str, offset: int) -> tuple[Any, ...]:
        size = struct.calcsize("<" + fmt)
        if offset < 0 or offset + size > len(data):
            raise ElfError("truncated ELF structure")
        return struct.unpack_from("<" + fmt, data, offset)

    def region(offset: int, size: int) -> bytes:
        if offset < 0 or size < 0 or offset + size > len(data):
            raise ElfError("ELF region is outside the file")
        return data[offset:offset + size]

    def string(table: bytes, offset: int) -> str:
        if offset < 0 or offset >= len(table):
            raise ElfError("invalid ELF string offset")
        end = table.find(b"\0", offset)
        if end < 0:
            raise ElfError("unterminated ELF string")
        try:
            return table[offset:end].decode("utf-8")
        except UnicodeError as error:
            raise ElfError("non-UTF-8 dynamic name") from error

    if len(data) < 16 or data[:4] != b"\x7fELF":
        raise ElfError("not an ELF file")
    if data[4:7] != b"\x02\x01\x01":
        raise ElfError("only little-endian ELF64 version 1 is supported")
    header = unpack("HHIQQQIHHHHHH", 16)
    elf_type, machine, _, _, phoff, shoff, _, ehsize, phsize, phnum, shsize, shnum, _ = header
    if ehsize != 64 or elf_type not in (2, 3):
        raise ElfError("expected an ELF64 executable or shared object")
    if not shoff or not shnum or shsize != 64:
        raise ElfError("missing/extended section headers cannot be fully audited")
    if phnum == 0xffff or (phnum and phsize != 56):
        raise ElfError("unsupported program header layout")
    region(shoff, shsize * shnum)
    region(phoff, phsize * phnum)
    sections = [unpack("IIQQQQIIQQ", shoff + i * shsize) for i in range(shnum)]
    interpreters = []
    programs = [unpack("IIQQQQQQ", phoff + index * phsize) for index in range(phnum)]
    for program in programs:
        if program[0] == 3:  # PT_INTERP
            interpreters.append(string(region(program[2], program[5]), 0))
    if len(interpreters) > 1:
        raise ElfError("multiple ELF interpreters")

    def virtual_offset(address: int, size: int) -> int:
        offsets = {segment[2] + address - segment[3] for segment in programs
                   if segment[0] == 1 and address >= segment[3]
                   and address + size <= segment[3] + segment[5]}
        if len(offsets) != 1:
            raise ElfError("dynamic address does not map to one file-backed PT_LOAD region")
        offset = offsets.pop()
        region(offset, size)
        return offset

    dynamic_programs = [program for program in programs if program[0] == 2]
    dynamic_sections = [section for section in sections if section[1] == 6]
    if len(dynamic_programs) != len(dynamic_sections) or len(dynamic_programs) > 1:
        raise ElfError("PT_DYNAMIC and SHT_DYNAMIC must describe the same single table")
    if dynamic_programs:
        program, section = dynamic_programs[0], dynamic_sections[0]
        if (program[2], program[3], program[5]) != (section[4], section[3], section[5]):
            raise ElfError("PT_DYNAMIC and SHT_DYNAMIC offset/address/size mismatch")
        if virtual_offset(program[3], program[5]) != program[2]:
            raise ElfError("PT_DYNAMIC disagrees with the PT_LOAD file mapping")
    dynamic_values: dict[int, list[int]] = {}
    version_counts = {0x6ffffffe: 0, 0x6ffffffd: 0}
    needed: list[str] = []
    requirements: list[dict[str, str]] = []
    definitions: set[str] = set()
    soname = None
    rpath: list[str] = []
    runpath: list[str] = []
    for section in sections:
        kind, offset, size, link, entry_size = section[1], section[4], section[5], section[6], section[9]
        if kind not in (6, 0x6ffffffe, 0x6ffffffd):
            continue
        region(offset, size)
        if link >= len(sections) or sections[link][1] != 3:
            raise ElfError("dynamic metadata has no linked string table")
        table_section = sections[link]
        table = region(table_section[4], table_section[5])
        if kind == 6:  # SHT_DYNAMIC
            if entry_size != 16 or size % 16:
                raise ElfError("invalid dynamic table size")
            terminated = False
            for pos in range(offset, offset + size, 16):
                tag, value = unpack("qQ", pos)
                if tag == 0:
                    terminated = True
                    break
                dynamic_values.setdefault(tag, []).append(value)
                if tag == 1:
                    needed.append(string(table, value))
                elif tag == 14:
                    soname = string(table, value)
                elif tag == 15:
                    rpath = string(table, value).split(":")
                elif tag == 29:
                    runpath = string(table, value).split(":")
            if not terminated:
                raise ElfError("unterminated dynamic table")
            continue
        pos, end = offset, offset + size
        while pos < end:
            version_counts[kind] += 1
            record_size = 16 if kind == 0x6ffffffe else 20
            if pos + record_size > end:
                raise ElfError("truncated GNU version record")
            if kind == 0x6ffffffe:  # SHT_GNU_verneed
                ver, count, file_name, aux, next_record = unpack("HHIII", pos)
                provider = string(table, file_name)
            else:
                ver, _, _, count, _, aux, next_record = unpack("HHHHIII", pos)
                provider = ""
            if ver != 1 or not count or aux < record_size:
                raise ElfError("invalid GNU version record")
            aux_pos = pos + aux
            for index in range(count):
                aux_size = 16 if kind == 0x6ffffffe else 8
                if aux_pos < pos + record_size or aux_pos + aux_size > end:
                    raise ElfError("GNU version auxiliary record is outside its section")
                if kind == 0x6ffffffe:
                    _, _, _, name, next_aux = unpack("IHHII", aux_pos)
                    requirements.append({"library": provider, "version": string(table, name)})
                else:
                    name, next_aux = unpack("II", aux_pos)
                    if index == 0:  # remaining entries are parent versions
                        definitions.add(string(table, name))
                if index < count - 1 and next_aux < aux_size:
                    raise ElfError("invalid GNU version auxiliary chain")
                aux_pos += next_aux
            if not next_record:
                break
            if next_record < record_size or pos + next_record >= end:
                raise ElfError("invalid GNU version record chain")
            pos += next_record
    # The loader uses program headers and DT_* addresses, not section types.
    # Require both views to agree so removing/retyping a section cannot hide
    # dependency or version requirements from this section-based parser.
    def single_dynamic(tag: int) -> int:
        values = dynamic_values.get(tag, [])
        if len(values) != 1:
            raise ElfError(f"missing or duplicate dynamic tag {tag:#x}")
        return values[0]

    if dynamic_sections:
        string_section = sections[dynamic_sections[0][6]]
        string_address, string_size = single_dynamic(5), single_dynamic(10)
        if (string_section[3], string_section[5]) != (string_address, string_size):
            raise ElfError("DT_STRTAB/DT_STRSZ disagree with the dynamic string section")
        if virtual_offset(string_address, string_size) != string_section[4]:
            raise ElfError("dynamic string table disagrees with the PT_LOAD mapping")
    for kind, pointer_tag, count_tag in ((0x6ffffffe, 0x6ffffffe, 0x6fffffff),
                                          (0x6ffffffd, 0x6ffffffc, 0x6ffffffd)):
        tables = [section for section in sections if section[1] == kind]
        declared = pointer_tag in dynamic_values or count_tag in dynamic_values
        if not tables and not declared:
            continue
        if not dynamic_sections or len(tables) != 1 or not declared:
            raise ElfError("GNU version section and dynamic version declaration disagree")
        table = tables[0]
        address, count = single_dynamic(pointer_tag), single_dynamic(count_tag)
        if table[3] != address or virtual_offset(address, table[5]) != table[4]:
            raise ElfError("dynamic GNU version address disagrees with the section/PT_LOAD mapping")
        if table[6] != dynamic_sections[0][6] or not count or version_counts[kind] != count:
            raise ElfError("dynamic GNU version count or linked string table mismatch")
    return {
        "machine": machine, "class": 64, "byteOrder": "little", "type": elf_type,
        "sha256": hashlib.sha256(data).hexdigest(), "needed": needed,
        "soname": soname, "rpath": rpath, "runpath": runpath,
        "interpreter": interpreters[0] if interpreters else None,
        "versionRequirements": requirements, "versionDefinitions": sorted(definitions),
    }


def confined_path(root: Path, relative: Path, *, target_links: bool = False) -> Path:
    """Resolve symlinks inside one tree; sysroot absolute links are target-relative."""
    if relative.is_absolute():
        raise ElfError("expected a tree-relative path")
    pending = list(relative.parts)
    completed: list[str] = []
    traversed = 0
    while pending:
        part = pending.pop(0)
        if part in ("", "."):
            continue
        if part == "..":
            if not completed:
                raise ElfError("path escapes its declared root")
            completed.pop()
            continue
        candidate = root.joinpath(*completed, part)
        if candidate.is_symlink():
            traversed += 1
            if traversed > 40:
                raise ElfError("symlink loop or excessive indirection")
            target = Path(os.readlink(candidate))
            if target.is_absolute():
                if not target_links:
                    raise ElfError("absolute package symlink has no relocatable target")
                completed = []
                target = Path(*target.parts[1:])
            pending = list(target.parts) + pending
        else:
            completed.append(part)
    return root.joinpath(*completed)


def audit_package(package_root: Path, *, target_arch: str = "aarch64", max_glibc: str = "2.31",
                  target_sysroot: Path | None = None) -> dict[str, Any]:
    package_root = package_root.resolve()
    target_sysroot = target_sysroot.resolve() if target_sysroot else None
    ceiling = version_tuple(max_glibc)
    machine, triplet = ARCHITECTURES[target_arch]
    findings: list[dict[str, str]] = []
    records: dict[str, dict[str, Any]] = {}
    edges: list[dict[str, str]] = []
    checks = {name: "PASS" for name in
              ("elfFormat", "architecture", "glibcCeiling", "neededClosure", "gnuVersions", "loaderPaths")}

    def finding(check: str, status: str, path: str, detail: str) -> None:
        findings.append({"check": check, "status": status, "path": path, "detail": detail})
        if checks[check] != "FAIL":
            checks[check] = status

    def label(path: Path, root: Path) -> str:
        return ("package/" if root == package_root else "sysroot/") + path.relative_to(root).as_posix()

    def inspect(path: Path, root: Path) -> tuple[str, dict[str, Any] | None]:
        name = label(path, root)
        if name in records:
            return name, records[name]
        try:
            record = read_elf(path)
        except (OSError, ValueError) as error:
            finding("elfFormat", "FAIL", name, str(error))
            for check in ("architecture", "glibcCeiling", "neededClosure", "gnuVersions", "loaderPaths"):
                finding(check, "UNVERIFIED", name, "cannot audit malformed or unsupported ELF metadata")
            return name, None
        records[name] = record
        if record["machine"] != machine:
            finding("architecture", "FAIL", name,
                    f"ELF machine {record['machine']} does not match {target_arch} ({machine})")
        for requirement in record["versionRequirements"]:
            version = requirement["version"]
            if version.startswith("GLIBC_"):
                number = version.removeprefix("GLIBC_")
                if re.fullmatch(r"[0-9]+(?:\.[0-9]+)+", number):
                    if version_tuple(number) > ceiling:
                        finding("glibcCeiling", "FAIL", name, f"requires {version}, exceeds GLIBC_{max_glibc}")
                elif root == package_root:
                    finding("glibcCeiling", "UNVERIFIED", name,
                            f"{version} is not a numeric public GLIBC version")
                # Private interfaces within the supplied target rootfs do not
                # raise its numeric GLIBC floor. Packaged consumers of those
                # interfaces remain UNVERIFIED; private ABI is not portable.
        return name, record

    if not package_root.is_dir():
        raise ValueError("package root must be an existing directory")
    if target_sysroot is not None and not target_sysroot.is_dir():
        raise ValueError("target sysroot must be an existing directory")
    if target_sysroot == package_root:
        raise ValueError("target sysroot and package root must be distinct")
    pending: list[tuple[Path, Path]] = []
    for path in sorted(package_root.rglob("*")):
        if path.is_symlink() or not path.is_file():
            continue
        try:
            with path.open("rb") as stream:
                is_elf = stream.read(4) == b"\x7fELF"
        except OSError as error:
            finding("elfFormat", "FAIL", label(path, package_root), str(error))
            continue
        if is_elf:
            pending.append((path, package_root))
    package_elf_count = len(pending)
    if not pending:
        finding("elfFormat", "FAIL", "package/", "no regular ELF files found")
        for check in ("architecture", "glibcCeiling", "neededClosure", "gnuVersions", "loaderPaths"):
            finding(check, "UNVERIFIED", "package/", "no ELF metadata was audited")
    processed: set[str] = set()
    while pending:
        path, root = pending.pop(0)
        name, record = inspect(path, root)
        if name in processed or record is None:
            continue
        processed.add(name)
        # The launcher puts package/lib in LD_LIBRARY_PATH. DT_RUNPATH is local
        # to a direct dependency. Inherited legacy DT_RPATH is not modeled.
        if record["rpath"] and not record["runpath"]:
            finding("loaderPaths", "UNVERIFIED", name, "legacy DT_RPATH inheritance is not modeled")
        search = [(package_root, Path("lib"))]
        declared_paths = record["runpath"] or record["rpath"]
        for raw in declared_paths:
            expanded = re.sub(r"\$(?:\{ORIGIN\}|ORIGIN(?=[^A-Za-z0-9_]|$))",
                              lambda _: str(path.parent), raw)
            origin_used = expanded != raw
            if not expanded or "$" in expanded or not Path(expanded).is_absolute():
                finding("loaderPaths", "UNVERIFIED", name, f"unsupported loader path: {raw!r}")
                continue
            if origin_used:
                search.append((root, Path(os.path.relpath(expanded, root))))
            elif target_sysroot:
                search.append((target_sysroot, Path(expanded.lstrip("/"))))
            else:
                finding("loaderPaths", "UNVERIFIED", name, "absolute loader path requires a target sysroot")
        if target_sysroot:
            for directory in (f"lib/{triplet}", f"usr/lib/{triplet}", "lib64", "usr/lib64", "lib", "usr/lib"):
                search.append((target_sysroot, Path(directory)))
        providers: dict[str, dict[str, Any]] = {}
        for needed in record["needed"]:
            if not needed or "/" in needed:
                finding("neededClosure", "UNVERIFIED", name, f"unsupported DT_NEEDED name: {needed!r}")
                continue
            resolved = None
            for search_root, directory in search:
                try:
                    candidate = confined_path(search_root, directory / needed,
                                              target_links=search_root == target_sysroot)
                except ElfError as error:
                    finding("loaderPaths", "FAIL", name, f"{needed}: {error}")
                    break
                if candidate.is_file():
                    resolved = (candidate, search_root)
                    break
            if resolved is None:
                status = "FAIL" if target_sysroot else "UNVERIFIED"
                finding("neededClosure", status, name, f"unresolved DT_NEEDED {needed}")
                if any(item["library"] == needed for item in record["versionRequirements"]):
                    finding("gnuVersions", status, name, f"version provider unavailable: {needed}")
                continue
            provider_name, provider = inspect(*resolved)
            edges.append({"consumer": name, "needed": needed, "provider": provider_name})
            if provider is not None:
                providers[needed] = provider
                pending.append(resolved)
        for requirement in record["versionRequirements"]:
            provider = providers.get(requirement["library"])
            if provider is None:
                if requirement["library"] not in record["needed"]:
                    finding("gnuVersions", "UNVERIFIED", name,
                            f"version provider {requirement['library']} is not a direct DT_NEEDED entry")
                continue
            if requirement["version"] not in provider["versionDefinitions"]:
                finding("gnuVersions", "FAIL", name,
                        f"{requirement['library']} does not provide {requirement['version']}")
        interpreter = record["interpreter"]
        if interpreter:
            if not interpreter.startswith("/"):
                finding("loaderPaths", "FAIL", name, "ELF interpreter is not an absolute target path")
            elif target_sysroot is None:
                finding("loaderPaths", "UNVERIFIED", name, "interpreter requires a target sysroot")
            else:
                try:
                    loader = confined_path(target_sysroot, Path(interpreter.lstrip("/")), target_links=True)
                    if not loader.is_file():
                        raise ElfError("ELF interpreter is absent from target sysroot")
                    inspect(loader, target_sysroot)
                    pending.append((loader, target_sysroot))
                except ElfError as error:
                    finding("loaderPaths", "FAIL", name, str(error))
    # A process namespace generally reuses an already loaded dependency name
    # or SONAME. Per-edge RUNPATH resolution must not pretend that incompatible
    # copies can simultaneously satisfy different consumers in that namespace.
    provider_aliases: dict[str, dict[str, set[str]]] = {}
    for edge in edges:
        provider = records.get(edge["provider"])
        if provider is None:
            continue
        for alias in {edge["needed"], provider["soname"]} - {None}:
            by_hash = provider_aliases.setdefault(alias, {})
            by_hash.setdefault(provider["sha256"], set()).add(edge["provider"])
    for alias, by_hash in sorted(provider_aliases.items()):
        if len(by_hash) > 1:
            paths = sorted(path for paths in by_hash.values() for path in paths)
            detail = f"ambiguous loader identity {alias}: multiple provider hashes at {', '.join(paths)}"
            finding("neededClosure", "UNVERIFIED", "package/", detail)
            finding("gnuVersions", "UNVERIFIED", "package/", detail)
    status = "FAIL" if "FAIL" in checks.values() else "UNVERIFIED" if "UNVERIFIED" in checks.values() else "PASS"
    return {
        "schemaVersion": 1, "status": status, "targetArchitecture": target_arch,
        "maximumGlibc": max_glibc, "targetSysrootSupplied": target_sysroot is not None,
        "packageElfCount": package_elf_count, "checks": checks, "findings": findings,
        "files": [{"path": name, **record} for name, record in sorted(records.items())],
        "dependencies": edges,
        "searchContract": "package/lib via LD_LIBRARY_PATH, direct RUNPATH, standard target-sysroot directories; no host libraries",
        "notVerified": ["individual symbol and struct ABI", "dlopen-only dependencies", "ld.so.cache/hwcaps/preload/LD_AUDIT overrides",
                        "kernel/driver UAPI", "device or production execution"],
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package-root", type=Path, required=True)
    parser.add_argument("--target-arch", choices=tuple(ARCHITECTURES), default="aarch64")
    parser.add_argument("--max-glibc", default="2.31")
    parser.add_argument("--target-sysroot", type=Path)
    parser.add_argument("--report", type=Path)
    options = parser.parse_args(argv)
    try:
        report = audit_package(options.package_root, target_arch=options.target_arch,
                               max_glibc=options.max_glibc, target_sysroot=options.target_sysroot)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    text = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
    if options.report:
        options.report.write_text(text, encoding="utf-8")
    print(text, end="")
    return {"PASS": 0, "FAIL": 1, "UNVERIFIED": 2}[report["status"]]


if __name__ == "__main__":
    raise SystemExit(main())
