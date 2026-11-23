#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "catch_amalgamated.hpp"
#include "util/RuntimeIsolation.h"

namespace {
class PrivateRuntimeRoots {
public:
    PrivateRuntimeRoots() {
        auto pattern = (std::filesystem::temp_directory_path() / "cosmo-runtime-XXXXXX").string();
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back('\0');
        const auto* result = mkdtemp(buffer.data());
        if (!result)
            throw std::runtime_error("cannot create test directory");
        base = std::filesystem::canonical(result);
        std::filesystem::create_directory(base / "data");
        std::filesystem::create_directory(base / "app");
        chmod((base / "data").c_str(), 0700);
    }
    ~PrivateRuntimeRoots() {
        std::error_code error;
        std::filesystem::remove_all(base, error);
    }
    std::vector<std::string> Arguments() const {
        return {"--isolated",  "--data-dir", (base / "data").string(), "--app-dir", (base / "app").string(),
                "--http-port", "18080",      "--websocket-port",       "18081"};
    }
    std::filesystem::path base;
};
}  // namespace

TEST_CASE("Runtime options preserve normal operation and provide side-effect-free help",
          "[runtime-isolation]") {
    const auto normal = cosmo::runtime::ParseRuntimeOptions({});
    CHECK_FALSE(normal.isolated);
    CHECK(normal.data_dir.empty());
    CHECK(normal.app_dir.empty());
    CHECK(normal.http_port == 0);
    CHECK(cosmo::runtime::ParseRuntimeOptions({"--help"}).show_help);
    CHECK(cosmo::runtime::ParseRuntimeOptions({"-h"}).show_help);
    CHECK_THROWS(cosmo::runtime::ParseRuntimeOptions({"--help", "--isolated"}));
    CHECK_THROWS(cosmo::runtime::ParseRuntimeOptions({"--isolate"}));
    CHECK_THROWS(cosmo::runtime::ParseRuntimeOptions({"--data-dir"}));
    CHECK_THROWS(cosmo::runtime::ParseRuntimeOptions({"--http-port", "18080"}));
    CHECK_THROWS(cosmo::runtime::ParseRuntimeOptions({"--isolated"}));
}

TEST_CASE("Runtime isolation requires complete separate private roots and explicit ports",
          "[runtime-isolation]") {
    PrivateRuntimeRoots roots;
    auto arguments = roots.Arguments();
    SECTION("valid private directories remain untouched") {
        std::ofstream(roots.base / "data" / "evidence.txt") << "keep";
        const auto options = cosmo::runtime::ParseRuntimeOptions(arguments);
        CHECK(options.isolated);
        CHECK(options.http_port == 18080);
        CHECK(options.websocket_port == 18081);
        CHECK(options.app_dir == (roots.base / "app").string());
        CHECK(std::filesystem::is_regular_file(roots.base / "data" / "evidence.txt"));
        CHECK_FALSE(std::filesystem::exists(roots.base / "data" / "conf"));
    }
    SECTION("incomplete option sets fail") {
        arguments.resize(7);
        CHECK_THROWS(cosmo::runtime::ParseRuntimeOptions(arguments));
    }
    SECTION("duplicate options fail") {
        arguments.push_back("--isolated");
        CHECK_THROWS(cosmo::runtime::ParseRuntimeOptions(arguments));
    }
    SECTION("port syntax, bounds and conflicts fail") {
        for (const auto* value : {"0", "80", "65536", "-1", "1x", "18080 "}) {
            arguments[6] = value;
            CHECK_THROWS(cosmo::runtime::ParseRuntimeOptions(arguments));
        }
        arguments[6] = "18081";
        CHECK_THROWS(cosmo::runtime::ParseRuntimeOptions(arguments));
    }
    SECTION("relative and absent roots fail without creating them") {
        arguments[2] = "relative-data";
        CHECK_THROWS(cosmo::runtime::ParseRuntimeOptions(arguments));
        arguments[2] = (roots.base / "absent").string();
        CHECK_THROWS(cosmo::runtime::ParseRuntimeOptions(arguments));
        CHECK_FALSE(std::filesystem::exists(roots.base / "absent"));
    }
    SECTION("root, application overlap and production ancestry fail") {
        arguments[2] = "/";
        CHECK_THROWS(cosmo::runtime::ParseRuntimeOptions(arguments));
        arguments[2] = (roots.base / "app").string();
        CHECK_THROWS(cosmo::runtime::ParseRuntimeOptions(arguments));
        arguments[2] = roots.base.string();
        CHECK_THROWS(cosmo::runtime::ParseRuntimeOptions(arguments));
    }
    SECTION("group accessible data is refused") {
        chmod((roots.base / "data").c_str(), 0750);
        CHECK_THROWS(cosmo::runtime::ParseRuntimeOptions(arguments));
    }
    SECTION("symbolic roots and nested aliases are refused") {
        std::filesystem::create_directory_symlink(roots.base / "data", roots.base / "alias");
        arguments[2] = (roots.base / "alias").string();
        CHECK_THROWS(cosmo::runtime::ParseRuntimeOptions(arguments));
        arguments[2] = (roots.base / "data").string();
        std::filesystem::create_directory_symlink(roots.base / "app", roots.base / "data" / "conf");
        CHECK_THROWS(cosmo::runtime::ParseRuntimeOptions(arguments));
    }
    SECTION("hard-linked mutable files are refused") {
        std::ofstream(roots.base / "app" / "outside") << "keep";
        std::filesystem::create_hard_link(roots.base / "app" / "outside", roots.base / "data" / "alias");
        CHECK_THROWS(cosmo::runtime::ParseRuntimeOptions(arguments));
    }
}

TEST_CASE("Runtime isolation lasts for its scope and restores ordinary behavior", "[runtime-isolation]") {
    CHECK_FALSE(cosmo::runtime::IsIsolatedRuntime());
    {
        const cosmo::runtime::RuntimeIsolationScope isolated(true);
        CHECK(cosmo::runtime::IsIsolatedRuntime());
        {
            const cosmo::runtime::RuntimeIsolationScope nested(true);
            CHECK(cosmo::runtime::IsIsolatedRuntime());
        }
        CHECK(cosmo::runtime::IsIsolatedRuntime());
    }
    CHECK_FALSE(cosmo::runtime::IsIsolatedRuntime());
}
