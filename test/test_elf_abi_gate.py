"""ABI admission regressions, including the shipped FFmpeg's real version needs."""
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools/package/elf_abi_gate.py"
SPEC = importlib.util.spec_from_file_location("elf_abi_gate", TOOL)
assert SPEC and SPEC.loader
abi = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(abi)


def elf64(path: Path, *, machine: int = 183, needed: tuple[str, ...] = (),
          requirements: dict[str, tuple[str, ...]] | None = None,
          definitions: tuple[str, ...] = (), interpreter: str | None = None,
          runpath: str | None = None, rpath: str | None = None,
          soname: str | None = None) -> None:
    """Construct ELF64 fixtures from the published dynamic/version record layouts.

    These are metadata fixtures, not executable code or admitted SDK artifacts.
    """
    strings = bytearray(b"\0")

    def name(value: str) -> int:
        offset = len(strings)
        strings.extend(value.encode() + b"\0")
        return offset

    dynamic = [(1, name(value)) for value in needed]
    if runpath is not None:
        dynamic.append((29, name(runpath)))
    if rpath is not None:
        dynamic.append((15, name(rpath)))
    if soname is not None:
        dynamic.append((14, name(soname)))
    verneed = bytearray()
    groups = list((requirements or {}).items())
    for index, (library, versions) in enumerate(groups):
        length = 16 + 16 * len(versions)
        verneed.extend(struct.pack("<HHIII", 1, len(versions), name(library), 16,
                                   length if index < len(groups) - 1 else 0))
        for vi, version in enumerate(versions):
            verneed.extend(struct.pack("<IHHII", 0, 0, vi + 2, name(version),
                                       16 if vi < len(versions) - 1 else 0))
    verdef = bytearray()
    for index, version in enumerate(definitions):
        verdef.extend(struct.pack("<HHHHIII", 1, 0, index + 2, 1, 0, 20,
                                  28 if index < len(definitions) - 1 else 0))
        verdef.extend(struct.pack("<II", name(version), 0))
    program_count = 2 + int(bool(interpreter))  # PT_LOAD, PT_DYNAMIC, optional PT_INTERP
    data = bytearray(b"\0" * (64 + 56 * program_count))
    if interpreter:
        interp_offset = len(data)
        interp_data = interpreter.encode() + b"\0"
        data.extend(interp_data)
        struct.pack_into("<IIQQQQQQ", data, 64 + 112, 3, 4, interp_offset, interp_offset, 0,
                         len(interp_data), len(interp_data), 1)
    sections = [(0,) * 10]

    def section(kind: int, payload: bytes, link: int = 0, entry_size: int = 0) -> None:
        offset = len(data)
        data.extend(payload)
        sections.append((0, kind, 2, offset, offset, len(payload), link, 0, 1, entry_size))

    section(3, strings)
    dynamic.extend([(5, sections[-1][3]), (10, len(strings))])
    if verneed:
        section(0x6ffffffe, verneed, 1)
        dynamic.extend([(0x6ffffffe, sections[-1][3]), (0x6fffffff, len(groups))])
    if verdef:
        section(0x6ffffffd, verdef, 1)
        dynamic.extend([(0x6ffffffc, sections[-1][3]), (0x6ffffffd, len(definitions))])
    dynamic.append((0, 0))
    section(6, b"".join(struct.pack("<qQ", *value) for value in dynamic), 1, 16)
    dynamic_section = sections[-1]
    section_offset = len(data)
    for value in sections:
        data.extend(struct.pack("<IIQQQQIIQQ", *value))
    data[:16] = b"\x7fELF\x02\x01\x01" + b"\0" * 9
    struct.pack_into("<HHIQQQIHHHHHH", data, 16, 3, machine, 1, 0,
                     64, section_offset, 0, 64, 56, program_count, 64, len(sections), 0)
    struct.pack_into("<IIQQQQQQ", data, 64, 1, 4, 0, 0, 0, len(data), len(data), 1)
    struct.pack_into("<IIQQQQQQ", data, 64 + 56, 2, 4, dynamic_section[4], dynamic_section[3], 0,
                     dynamic_section[5], dynamic_section[5], 8)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


class ElfAbiGateTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.package = self.base / "package"
        self.sysroot = self.base / "sysroot"
        self.package.mkdir()
        self.sysroot.mkdir()

    def audit(self, *, with_sysroot: bool = True) -> dict:
        return abi.audit_package(self.package, target_sysroot=self.sysroot if with_sysroot else None)

    def complete_fixture(self) -> None:
        elf64(self.package / "bin/engine", needed=("libc.so.6", "libstdc++.so.6"),
              requirements={"libc.so.6": ("GLIBC_2.31",),
                            "libstdc++.so.6": ("GLIBCXX_3.4.30", "CXXABI_1.3.13")},
              interpreter="/lib/ld-linux-aarch64.so.1")
        elf64(self.sysroot / "lib/aarch64-linux-gnu/libc.so.6", definitions=("GLIBC_2.31",))
        elf64(self.package / "lib/libstdc++.so.6", needed=("libc.so.6",),
              requirements={"libc.so.6": ("GLIBC_2.31",)},
              definitions=("GLIBCXX_3.4.30", "CXXABI_1.3.13"))
        elf64(self.sysroot / "lib/ld-linux-aarch64.so.1")

    def test_complete_dependency_and_version_contract_passes(self) -> None:
        self.complete_fixture()
        report = self.audit()
        self.assertEqual(report["status"], "PASS", report["findings"])
        self.assertEqual(len(report["files"]), 4)
        self.assertEqual(len(report["dependencies"]), 3)
        self.assertTrue(all(entry["sha256"] for entry in report["files"]))

    def test_missing_target_sysroot_is_unverified_not_pass(self) -> None:
        self.complete_fixture()
        report = self.audit(with_sysroot=False)
        self.assertEqual(report["status"], "UNVERIFIED")
        self.assertEqual(report["checks"]["neededClosure"], "UNVERIFIED")
        self.assertEqual(report["checks"]["gnuVersions"], "UNVERIFIED")
        self.assertEqual(report["checks"]["loaderPaths"], "UNVERIFIED")

    def test_cpp_provider_must_supply_both_required_version_families(self) -> None:
        self.complete_fixture()
        for definitions, absent in [(("GLIBCXX_3.4.29", "CXXABI_1.3.13"), "GLIBCXX_3.4.30"),
                                    (("GLIBCXX_3.4.30", "CXXABI_1.3.12"), "CXXABI_1.3.13")]:
            with self.subTest(absent=absent):
                elf64(self.package / "lib/libstdc++.so.6", definitions=definitions)
                report = self.audit()
                self.assertEqual(report["status"], "FAIL")
                self.assertTrue(any(absent in item["detail"] for item in report["findings"]))

    def test_transitive_dependency_cannot_be_satisfied_from_host(self) -> None:
        elf64(self.package / "bin/engine", needed=("libfirst.so",))
        elf64(self.package / "lib/libfirst.so", needed=("libmissing.so",))
        report = self.audit()
        self.assertEqual(report["checks"]["neededClosure"], "FAIL")
        self.assertTrue(any(item["path"] == "package/lib/libfirst.so" for item in report["findings"]))

    def test_glibc_ceiling_applies_to_every_elf_in_package(self) -> None:
        elf64(self.package / "bin/engine")
        elf64(self.package / "lib/plugin.so", needed=("libm.so.6",),
              requirements={"libm.so.6": ("GLIBC_2.35",)})
        report = self.audit(with_sysroot=False)
        self.assertEqual(report["status"], "FAIL")
        self.assertEqual(report["checks"]["glibcCeiling"], "FAIL")

    def test_sysroot_provider_architecture_is_checked(self) -> None:
        elf64(self.package / "bin/engine", needed=("libc.so.6",))
        elf64(self.sysroot / "lib/aarch64-linux-gnu/libc.so.6", machine=62)
        self.assertEqual(self.audit()["checks"]["architecture"], "FAIL")

    def test_package_symlink_cannot_escape_to_host(self) -> None:
        elf64(self.package / "bin/engine", needed=("libescape.so",))
        elf64(self.base / "outside.so")
        (self.package / "lib").mkdir()
        (self.package / "lib/libescape.so").symlink_to("../../outside.so")
        report = self.audit()
        self.assertEqual(report["checks"]["loaderPaths"], "FAIL")
        self.assertFalse(any("outside" in item["path"] for item in report["files"]))

    def test_sysroot_absolute_symlink_stays_in_sysroot(self) -> None:
        elf64(self.package / "bin/engine", needed=("libc.so.6",))
        elf64(self.sysroot / "usr/lib/libc-2.31.so")
        (self.sysroot / "lib").mkdir()
        (self.sysroot / "lib/libc.so.6").symlink_to("/usr/lib/libc-2.31.so")
        report = self.audit()
        self.assertEqual(report["status"], "PASS", report["findings"])
        self.assertEqual(report["dependencies"][0]["provider"], "sysroot/usr/lib/libc-2.31.so")

    def test_origin_runpath_is_resolved_for_direct_dependencies(self) -> None:
        elf64(self.package / "bin/engine", needed=("libcustom.so",), runpath="$ORIGIN/../custom")
        elf64(self.package / "custom/libcustom.so")
        self.assertEqual(self.audit()["status"], "PASS")

    def test_legacy_rpath_is_not_overclaimed(self) -> None:
        elf64(self.package / "bin/engine", rpath="$ORIGIN/../lib")
        self.assertEqual(self.audit()["checks"]["loaderPaths"], "UNVERIFIED")

    def test_unsupported_dynamic_tokens_are_unverified(self) -> None:
        elf64(self.package / "bin/engine", runpath="$ORIGIN/$LIB")
        self.assertEqual(self.audit()["status"], "UNVERIFIED")

    def test_private_glibc_abi_is_not_a_numeric_version_pass(self) -> None:
        elf64(self.package / "lib/private.so", needed=("libc.so.6",),
              requirements={"libc.so.6": ("GLIBC_PRIVATE",)})
        elf64(self.sysroot / "lib/libc.so.6", definitions=("GLIBC_PRIVATE",))
        self.assertEqual(self.audit()["checks"]["glibcCeiling"], "UNVERIFIED")

    def test_private_interfaces_inside_target_rootfs_do_not_raise_numeric_floor(self) -> None:
        elf64(self.package / "engine", needed=("libc.so.6",),
              requirements={"libc.so.6": ("GLIBC_2.31",)})
        elf64(self.sysroot / "lib/libc.so.6", needed=("ld-linux-aarch64.so.1",),
              requirements={"ld-linux-aarch64.so.1": ("GLIBC_PRIVATE",)},
              definitions=("GLIBC_2.31",))
        elf64(self.sysroot / "lib/ld-linux-aarch64.so.1", definitions=("GLIBC_PRIVATE",))
        self.assertEqual(self.audit()["status"], "PASS")

    def test_origin_prefix_does_not_expand_an_unknown_token(self) -> None:
        elf64(self.package / "engine", runpath="$ORIGIN_UNKNOWN/lib")
        self.assertEqual(self.audit()["checks"]["loaderPaths"], "UNVERIFIED")

    def test_no_elf_and_truncated_elf_fail_closed(self) -> None:
        self.assertEqual(self.audit()["status"], "FAIL")
        (self.package / "truncated").write_bytes(b"\x7fELF\x02\x01\x01")
        self.assertEqual(self.audit()["checks"]["elfFormat"], "FAIL")

    def test_sectionless_elf_does_not_bypass_version_check(self) -> None:
        path = self.package / "engine"
        elf64(path)
        data = bytearray(path.read_bytes())
        struct.pack_into("<Q", data, 40, 0)
        path.write_bytes(data)
        self.assertEqual(self.audit()["checks"]["elfFormat"], "FAIL")

    def test_symlink_loop_fails_without_hanging(self) -> None:
        elf64(self.package / "engine", needed=("libloop.so",))
        (self.package / "lib").mkdir()
        (self.package / "lib/libloop.so").symlink_to("libloop.so")
        self.assertEqual(self.audit()["status"], "FAIL")

    def test_cli_exit_codes_and_machine_readable_report(self) -> None:
        self.complete_fixture()
        output = self.base / "report.json"
        command = [sys.executable, str(TOOL), "--package-root", str(self.package), "--report", str(output)]
        unknown = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(unknown.returncode, 2, unknown.stderr)
        self.assertEqual(json.loads(output.read_text())["status"], "UNVERIFIED")
        passed = subprocess.run(command + ["--target-sysroot", str(self.sysroot)], capture_output=True, text=True)
        self.assertEqual(passed.returncode, 0, passed.stderr)
        self.assertEqual(json.loads(passed.stdout), json.loads(output.read_text()))
        failed = subprocess.run(command + ["--max-glibc", "2.30"], capture_output=True, text=True)
        self.assertEqual(failed.returncode, 1, failed.stderr)

    def test_different_runpaths_cannot_claim_two_incompatible_same_name_providers(self) -> None:
        elf64(self.package / "bin/engine", needed=("liba.so", "libb.so"))
        for suffix, version, directory in (("a", "API_1", "first"), ("b", "API_2", "second")):
            elf64(self.package / f"lib/lib{suffix}.so", needed=("libshared.so",),
                  requirements={"libshared.so": (version,)}, runpath=f"$ORIGIN/../{directory}")
            elf64(self.package / directory / "libshared.so", definitions=(version,))
        report = self.audit()
        self.assertEqual(report["status"], "UNVERIFIED")
        self.assertTrue(any("ambiguous loader identity libshared.so" in item["detail"]
                            for item in report["findings"]))

    def test_aliases_with_same_soname_are_also_ambiguous(self) -> None:
        elf64(self.package / "bin/engine", needed=("libfirst.so", "libsecond.so"))
        elf64(self.package / "lib/libfirst.so", soname="libshared.so", definitions=("API_1",))
        elf64(self.package / "lib/libsecond.so", soname="libshared.so", definitions=("API_2",))
        report = self.audit()
        self.assertEqual(report["status"], "UNVERIFIED")
        self.assertTrue(any("ambiguous loader identity libshared.so" in item["detail"]
                            for item in report["findings"]))

    def test_identical_soname_provider_bytes_are_not_ambiguous(self) -> None:
        elf64(self.package / "bin/engine", needed=("libfirst.so", "libsecond.so"))
        first = self.package / "lib/libfirst.so"
        elf64(first, soname="libshared.so", definitions=("API_1",))
        (self.package / "lib/libsecond.so").write_bytes(first.read_bytes())
        self.assertEqual(self.audit()["status"], "PASS")

    def test_program_dynamic_offset_must_match_section(self) -> None:
        path = self.package / "engine"
        elf64(path)
        data = bytearray(path.read_bytes())
        # PT_DYNAMIC.p_offset differs while the section-based view stays intact.
        offset = struct.unpack_from("<Q", data, 64 + 56 + 8)[0]
        struct.pack_into("<Q", data, 64 + 56 + 8, offset + 16)
        path.write_bytes(data)
        report = self.audit()
        self.assertEqual(report["checks"]["elfFormat"], "FAIL")
        self.assertTrue(any("PT_DYNAMIC" in item["detail"] for item in report["findings"]))

    def test_dynamic_version_count_must_match_parsed_records(self) -> None:
        path = self.package / "engine"
        elf64(path, needed=("libc.so.6",), requirements={"libc.so.6": ("GLIBC_2.31",)})
        data = bytearray(path.read_bytes())
        offset = struct.unpack_from("<Q", data, 64 + 56 + 8)[0]
        while True:
            tag, _ = struct.unpack_from("<qQ", data, offset)
            if tag == 0x6fffffff:
                struct.pack_into("<Q", data, offset + 8, 2)
                break
            self.assertNotEqual(tag, 0)
            offset += 16
        path.write_bytes(data)
        self.assertEqual(self.audit()["checks"]["elfFormat"], "FAIL")

    def test_retyping_real_dynamic_or_version_sections_cannot_hide_requirements(self) -> None:
        source = ROOT / "prebuild/ffmpeg/aarch64/lib/libavformat.so.58.76.100"
        if not source.is_file():
            self.skipTest("tracked FFmpeg artifact unavailable in this source checkout")
        # Cover both the original all-metadata attack and version-only changes
        # which leave SHT_DYNAMIC intact while DT_VERNEED/VERDEF still point to it.
        for hidden in ({6, 0x6ffffffe, 0x6ffffffd}, {0x6ffffffe}, {0x6ffffffd}):
            with self.subTest(hidden=hidden):
                data = bytearray(source.read_bytes())
                header = struct.unpack_from("<HHIQQQIHHHHHH", data, 16)
                for index in range(header[11]):
                    offset = header[5] + index * header[10]
                    kind = struct.unpack_from("<I", data, offset + 4)[0]
                    if kind in hidden:
                        struct.pack_into("<I", data, offset + 4, 1)  # SHT_PROGBITS
                (self.package / "libavformat.so.58").write_bytes(data)
                report = self.audit()
                self.assertEqual(report["status"], "FAIL")
                self.assertEqual(report["checks"]["elfFormat"], "FAIL")
                self.assertNotEqual(report["checks"]["glibcCeiling"], "PASS")

    def test_shipped_aarch64_ffmpeg_has_real_glibc_235_requirement(self) -> None:
        path = ROOT / "prebuild/ffmpeg/aarch64/lib/libavformat.so.58.76.100"
        if not path.is_file():
            self.skipTest("tracked FFmpeg artifact unavailable in this source checkout")
        record = abi.read_elf(path)
        self.assertEqual(record["machine"], 183)
        self.assertIn({"library": "libm.so.6", "version": "GLIBC_2.35"}, record["versionRequirements"])
        self.assertIn({"library": "libc.so.6", "version": "GLIBC_2.34"}, record["versionRequirements"])
        (self.package / "libavformat.so.58").write_bytes(path.read_bytes())
        report = self.audit(with_sysroot=False)
        self.assertEqual(report["checks"]["glibcCeiling"], "FAIL")


if __name__ == "__main__":
    unittest.main()
