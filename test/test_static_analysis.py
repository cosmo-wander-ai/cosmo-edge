"""Exercise static-analysis file selection, failures and Catch2 assertions."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest

REPOSITORY = Path(__file__).resolve().parents[1]
REAL_CPPCHECK = shutil.which("cppcheck")
BASH = shutil.which("bash")


class StaticAnalysisTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="cppcheck repo ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        for directory in ("scripts", "src", "test", "bin", "tmp"):
            (self.root / directory).mkdir()
        shutil.copy2(REPOSITORY / "scripts/static_analysis.sh", self.root / "scripts")
        (self.root / "tools/cppcheck").mkdir(parents=True)
        shutil.copy2(REPOSITORY / "tools/cppcheck/Catch2Model.h", self.root / "tools/cppcheck")
        shutil.copy2(REPOSITORY / ".cppcheck-suppressions", self.root)
        # Keep fixture commands independent of the host's installed analyzers.
        for name in ("bash", "dirname", "git", "find", "sort", "nproc", "mktemp",
                     "rm"):
            (self.root / "bin" / name).symlink_to(shutil.which(name))
        self.log = self.root / "calls.jsonl"
        self.env = dict(os.environ, PATH=str(self.root / "bin"),
                        TMPDIR=str(self.root / "tmp"), CALL_LOG=str(self.log))
        self.install_cppcheck()
        self.git("init", "-q")
        self.commit()

    def write(self, relative, content="int answer() { return 42; }\n"):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
        return path

    def write_executable(self, relative, content):
        path = self.write(relative, content)
        path.chmod(0o755)

    def install_cppcheck(self):
        self.write_executable("bin/cppcheck", f"#!{sys.executable}\n" + textwrap.dedent("""\
            import json, os, sys
            args = sys.argv[1:]
            with open(os.environ['CALL_LOG'], 'a') as log:
                log.write(json.dumps(['cppcheck', *args]) + '\\n')
            if args == ['--version']:
                print('Cppcheck 2.13.0')
                sys.exit(int(os.environ.get('VERSION_EXIT', '0')))
            sys.exit(int(os.environ.get('CPPCHECK_EXIT', '0')))
            """))

    def git(self, *args):
        result = subprocess.run([str(self.root / "bin/git"), *args], cwd=self.root,
                                env=self.env, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout.strip()

    def commit(self):
        self.git("-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                 "-c", "commit.gpgsign=false", "-c", "core.hooksPath=/dev/null",
                 "commit", "--allow-empty", "-qm", "fixture")

    def run_command(self, command, expected=0, **env):
        self.log.unlink(missing_ok=True)
        result = subprocess.run(command, cwd=self.root, env={**self.env, **env},
                                text=True, capture_output=True)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        self.assertEqual(list((self.root / "tmp").iterdir()), [], "file-list leak")
        return result.stdout + result.stderr

    def analyze(self, *args, expected=0, **env):
        return self.run_command([BASH, str(self.root / "scripts/static_analysis.sh"), *args],
                                expected=expected, **env)

    def calls(self):
        records = [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []
        return [record[1:] for record in records if record[1:] != ["--version"]]

    def assert_files(self, expected):
        calls = self.calls()
        self.assertEqual(len(calls), 1, calls)
        files = {arg for arg in calls[0]
                 if arg.startswith(str(self.root) + "/") and arg.endswith((".h", ".cc"))}
        self.assertEqual(files, {str(self.root / name) for name in expected})
        return calls[0]

    def test_profiles_and_full_scope(self):
        included = ("src/top.cc", "src/nested/with space.h", "test/check.cc")
        excluded = ("other.cc", "src/3rd/vendor.cc", "src/build/generated.h",
                    "test/catch_amalgamated.cc", "src/ignore.cpp")
        for name in (*included, *excluded):
            self.write(name)
        for profile, enabled in ((None, "warning,style,performance,portability"),
                                 ("full", "warning,style,performance,portability"),
                                 ("commit", "warning")):
            with self.subTest(profile=profile):
                self.analyze("--cppcheck", *(["--profile", profile] if profile else []))
                args = self.assert_files(included)
                self.assertIn("--enable=" + enabled, args)
                self.assertIn("--error-exitcode=1", args)
                self.assertIn("--suppressions-list=" + str(self.root / ".cppcheck-suppressions"), args)

    def test_staged_scope_spaces_and_deleted_files(self):
        self.write("src/deleted.cc")
        self.git("add", "src/deleted.cc")
        self.commit()
        self.git("rm", "src/deleted.cc")
        included = ("src/space name.cc", 'test/quoted "name".h', "other/staged.cc")
        for name in (*included, "src/ignored.cpp", "src/unstaged.cc"):
            self.write(name)
        self.git("add", "--", *included, "src/ignored.cpp")
        self.analyze("--cppcheck", "--staged")
        self.assert_files(included)

    def test_base_ref_scope_renames_and_deleted_files(self):
        for name in ("src/deleted.cc", "src/old.cc", "src/unchanged.h"):
            self.write(name)
        self.git("add", "src")
        self.commit()
        base = self.git("rev-parse", "HEAD")
        self.git("rm", "src/deleted.cc")
        self.git("mv", "src/old.cc", "src/renamed file.cc")
        included = ("src/top.h", "src/nested/change.cc", "test/top.cc", "test/nested/test.h")
        for name in (*included, "other/ignored.cc", "src/ignored.cpp"):
            self.write(name)
        self.git("add", "src", "test", "other")
        self.commit()
        self.write("src/staged-only.cc")
        self.git("add", "src/staged-only.cc")
        self.analyze("--cppcheck", "--base-ref", base)
        self.assert_files((*included, "src/renamed file.cc"))

    def test_empty_and_deleted_only_sets_succeed_without_analysis(self):
        for args in ((), ("--staged",), ("--base-ref", "HEAD")):
            self.assertIn("No files to analyze", self.analyze("--cppcheck", *args))
            self.assertEqual(self.calls(), [])
        self.write("src/deleted.cc")
        self.git("add", "src/deleted.cc")
        self.commit()
        base = self.git("rev-parse", "HEAD")
        self.git("rm", "src/deleted.cc")
        self.analyze("--cppcheck", "--staged")
        self.assertEqual(self.calls(), [])
        self.commit()
        self.analyze("--cppcheck", "--base-ref", base)
        self.assertEqual(self.calls(), [])

    def test_collection_errors_fail_instead_of_empty_success(self):
        (self.root / ".git").rename(self.root / "git-backup")
        self.analyze("--cppcheck", "--staged", expected=129)
        self.assertEqual(self.calls(), [])
        (self.root / "src").rmdir()
        self.analyze("--all", expected=1)
        self.assertEqual(self.calls(), [])

    def test_tool_failures_propagate_without_success_message(self):
        self.write("src/clean.cc")
        for status in (1, 17):
            output = self.analyze("--cppcheck", expected=status, CPPCHECK_EXIT=str(status))
            self.assertNotIn("No issues found", output)
        output = self.analyze("--all", expected=1, VERSION_EXIT="23")
        self.assertNotIn("No issues found", output)
        self.assertEqual(self.calls(), [])

    @unittest.skipUnless(REAL_CPPCHECK, "cppcheck is not installed")
    def test_real_catch_assertions_are_analyzed_without_hiding_diagnostics(self):
        version = subprocess.check_output([REAL_CPPCHECK, "--version"], text=True)
        match = re.search(r"Cppcheck (\d+)\.(\d+)", version)
        if not match or tuple(map(int, match.groups())) < (2, 13):
            self.skipTest("Cppcheck 2.13 or newer is required for real Catch2 analysis")
        (self.root / "bin/cppcheck").unlink()
        (self.root / "bin/cppcheck").symlink_to(REAL_CPPCHECK)
        shutil.copy2(REPOSITORY / "test/catch_amalgamated.hpp", self.root / "test")
        source = self.write("test/assertions.cc", textwrap.dedent('''\
            #include "catch_amalgamated.hpp"
            #include <stdexcept>
            bool Ready();
            void MayThrow();
            void Guard(int *p) { REQUIRE(p); *p = 1; }
            void GuardFalse(int *p) { REQUIRE_FALSE(p == nullptr); *p = 1; }
            TEST_CASE("evaluated assertions", "[analysis]") {
                const bool used = Ready();
                REQUIRE(used);
                CHECK(used);
                CHECK_FALSE(!used);
                REQUIRE_NOTHROW(MayThrow());
                CHECK_NOTHROW(MayThrow());
                REQUIRE_THROWS(MayThrow());
                CHECK_THROWS(MayThrow());
                REQUIRE_THROWS_AS(MayThrow(), std::runtime_error);
                CHECK_THROWS_AS(MayThrow(), std::runtime_error);
                SECTION("first") { CHECK(used); }
                SECTION("second") { CHECK_FALSE(!used); }
            }
        '''))
        self.git("add", "test/assertions.cc")
        self.analyze("--cppcheck", "--staged", "--profile", "full")

        bad_assertions = [f"{name}(1 / zero);" for name in
                          ("REQUIRE", "REQUIRE_FALSE", "CHECK", "CHECK_FALSE", "REQUIRE_NOTHROW",
                           "CHECK_NOTHROW", "REQUIRE_THROWS", "CHECK_THROWS")]
        bad_assertions += [f"{name}(1 / zero, int);" for name in
                           ("REQUIRE_THROWS_AS", "CHECK_THROWS_AS")]
        bad_source = '#include "catch_amalgamated.hpp"\nbool Ready();\n'
        for index, assertion in enumerate(bad_assertions):
            bad_source += f'void bad_{index}() {{ int zero = 0; {assertion} }}\n'
        bad_source += textwrap.dedent('''\
            void nonfatal() { int *p = nullptr; CHECK(p); *p = 1; }
            void unused() { int not_read = 1; }
            void comparison() { CHECK(Ready() >= false); }
        ''')
        source.write_text(bad_source)
        output = self.analyze("--cppcheck", "--staged", "--profile", "full", expected=1)
        self.assertGreaterEqual(output.count("[error] zerodiv"), len(bad_assertions))
        for diagnostic in ("nullPointer", "unreadVariable", "comparisonOfFuncReturningBoolError"):
            self.assertIn(diagnostic, output)


if __name__ == "__main__":
    unittest.main()
