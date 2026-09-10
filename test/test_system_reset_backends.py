"""Exercise the real reset implementation without ever executing a reboot.

Run with Python 3 and a C++17 compiler (CXX may select the compiler).
Only OS execution, logging and delays are stubbed; reset and filesystem logic
are compiled from src/platform/SystemReboot.cc for each supported backend.
"""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ResetBackendTest(unittest.TestCase):
    def test_reset_lifecycle(self):
        compiler = (os.environ.get("CXX") or shutil.which("c++")
                    or shutil.which("clang++") or shutil.which("cl"))
        self.assertTrue(compiler, "C++17 compiler required; set CXX")
        with tempfile.TemporaryDirectory(prefix="cosmo-reset-") as tmp:
            root = Path(tmp)
            stubs = {
                "sys/reboot.h": "#pragma once\n",
                "unistd.h": "#pragma once\ninline void sync() {}\n",
                "util/Log.h": """#pragma once
#define LOG_INFO(...) ((void)0)
#define LOG_WARN(...) ((void)0)
#define LOG_ERRO(...) ((void)0)
namespace cosmo::log { inline void FlushLog() {} }
""",
                "util/TimingConstants.h": """#pragma once
#include <chrono>
namespace cosmo::timing {
constexpr auto kServiceReadyDelay = std::chrono::milliseconds(0);
constexpr auto kRebootGracePeriod = std::chrono::milliseconds(0);
}
""",
            }
            for name, content in stubs.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content, encoding="utf-8")
            harness = root / "reset_test.cc"
            harness.write_text(r'''
#include "platform/SystemReboot.h"
#include "util/Exec.h"
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs = std::filesystem;
int reboot_calls = 0;
namespace cosmo::util {
int Exec(const std::vector<std::string>& argv, std::string&) {
    if (argv != std::vector<std::string>{"reboot"}) return 127;
    ++reboot_calls;
    return 0;
}
}
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    const fs::path root = argv[1];
    const bool device = std::string(argv[2]) == "device";
    fs::create_directories(root / "conf");
    fs::create_directories(root / "model-guard");
    std::ofstream(root / "conf" / "settings.json") << "settings";
    std::ofstream(root / "model-guard" / "certificate") << "authorization";
    {
        cosmo::platform::RebootManager manager;
        manager.Reset("backend regression", root.string());
    } // Joins the real asynchronous reset task before checking effects.
    const bool cleared = !fs::exists(root / "conf");
    std::string authorization;
    std::ifstream(root / "model-guard" / "certificate") >> authorization;
    if (cleared != device || reboot_calls != (device ? 1 : 0) ||
        authorization != "authorization") {
        std::cerr << "cleared=" << cleared << " reboot_calls=" << reboot_calls
                  << " expected_device=" << device << '\n';
        return 1;
    }
}
''', encoding="utf-8")
            for backend in ("CPU", "RKNN", "SOPHON"):
                with self.subTest(backend=backend):
                    binary = root / (backend + (".exe" if os.name == "nt" else ""))
                    command = [compiler, "-std=c++17", "-pthread",
                               f"-DCOSMO_NN_USE_{backend}_BACKEND", "-I", str(root),
                               "-I", str(ROOT / "src"),
                               str(ROOT / "src/platform/SystemReboot.cc"),
                               str(harness), "-o", str(binary)]
                    if Path(compiler).name.lower() in ("cl", "cl.exe"):
                        command = [compiler, "/nologo", "/std:c++17", "/EHsc",
                                   f"/DCOSMO_NN_USE_{backend}_BACKEND",
                                   "/I" + str(root), "/I" + str(ROOT / "src"),
                                   str(ROOT / "src/platform/SystemReboot.cc"),
                                   str(harness), "/Fe:" + str(binary)]
                    build = subprocess.run(command, cwd=root, capture_output=True, text=True,
                                           errors="replace")
                    self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
                    result = subprocess.run(
                        [str(binary), str(root / (backend + "-data")),
                         "host" if backend == "CPU" else "device"],
                        capture_output=True, text=True, timeout=15)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
