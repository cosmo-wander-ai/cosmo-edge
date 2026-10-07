#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include "catch_amalgamated.hpp"
#include "util/BoundedProcess.h"

using namespace std::chrono_literals;
namespace {
using Clock = std::chrono::steady_clock;
auto Execute(const std::string& code, const std::string& input = "", std::chrono::milliseconds timeout = 5s) {
    return cosmo::util::RunBoundedProcess({"/usr/bin/python3", "-c", code}, input, Clock::now() + timeout);
}
}  // namespace

TEST_CASE("Bounded child pumps both outputs while stdin pipe is full", "[visual-preparation][process]") {
    auto result = Execute(
        "import sys\nsys.stderr.write('e'*4000);sys.stderr.flush()\n"
        "sys.stdout.write('o'*60000);sys.stdout.flush()\n"
        "print(len(sys.stdin.buffer.read()))",
        std::string(50000, 'i'));
    REQUIRE(result.failure.empty());
    REQUIRE(result.exitCode == 0);
    REQUIRE(result.output == std::string(60000, 'o') + "50000\n");
    REQUIRE(result.error == std::string(4000, 'e'));
}

TEST_CASE("Bounded child argv is literal and early exit cannot SIGPIPE the engine",
          "[visual-preparation][process]") {
    const std::string literal = "a 'quoted' $(printf injected) ; $HOME";
    auto result =
        cosmo::util::RunBoundedProcess({"/usr/bin/python3", "-c", "import sys; print(sys.argv[1])", literal},
                                       std::string(64000, 'x'), Clock::now() + 5s);
    REQUIRE(result.failure.empty());
    REQUIRE(result.exitCode == 0);
    REQUIRE(result.output == literal + "\n");
    auto failed = Execute("import sys; sys.exit(7)", std::string(64000, 'x'));
    REQUIRE(failed.exitCode == 7);
}

TEST_CASE("Bounded child times out and caps noisy stdout or stderr", "[visual-preparation][process]") {
    const int mode = GENERATE(0, 1, 2);
    const std::vector<std::string> scripts{"import time; time.sleep(30)",
                                           "import sys; sys.stdout.write('x'*100000)",
                                           "import sys; sys.stderr.write('x'*100000)"};
    auto start  = Clock::now();
    auto result = Execute(scripts[mode], "", mode == 0 ? 100ms : 5s);
    REQUIRE(result.failure == (mode == 0 ? "process_timeout" : "process_output_limit"));
    REQUIRE(result.output.size() <= 65536);
    REQUIRE(result.error.size() <= 8192);
    REQUIRE(Clock::now() - start < 3s);
}

TEST_CASE("Bounded child cancellation kills descendants retaining output pipes",
          "[visual-preparation][process]") {
    auto start = Clock::now();
    auto result =
        cosmo::util::RunBoundedProcess({"/usr/bin/python3", "-c",
                                        "import os,sys,time\nchild=os.fork()\nif child:\n "
                                        "print(child,flush=True)\n sys.exit(0)\ntime.sleep(30)"},
                                       "", start + 5s, [&] { return Clock::now() - start > 500ms; });
    REQUIRE(result.failure == "process_cancelled");
    REQUIRE_FALSE(result.output.empty());
    const auto child = std::stoi(result.output);
    auto stat        = std::filesystem::path("/proc") / std::to_string(child) / "stat";
    bool terminated  = false;
    for (int i = 0; i < 100; ++i) {
        if (!std::filesystem::exists(stat)) {
            terminated = true;
            break;
        }
        std::ifstream input(stat);
        std::string line;
        std::getline(input, line);
        const auto end = line.rfind(')');
        if (end != std::string::npos && line.size() > end + 2 && line[end + 2] == 'Z') {
            terminated = true;
            break;
        }
        std::this_thread::sleep_for(10ms);
    }
    REQUIRE(terminated);
}
