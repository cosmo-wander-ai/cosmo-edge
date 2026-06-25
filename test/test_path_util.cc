#include "catch_amalgamated.hpp"
/*
 * test_path_util.cc — PathUtil unit tests
 *
 * Tests path accessors after OverrideRootPathForTest().
 */
#include <filesystem>

#include "util/PathUtil.h"

using namespace cosmo::path;

namespace {

struct TestPathFixture {
    std::string test_dir;

    TestPathFixture() {
        test_dir = "/tmp/cosmo_path_test_" +
                   std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(test_dir);
        OverrideRootPathForTest(test_dir, test_dir);
    }

    ~TestPathFixture() {
        std::filesystem::remove_all(test_dir);
    }
};

}  // namespace

TEST_CASE("PathUtil: GetCfgPath returns non-empty path", "[path-util]") {
    TestPathFixture fix;
    auto cfg = GetCfgPath();
    REQUIRE(!cfg.empty());
}

TEST_CASE("PathUtil: GetCfgPath with subPath appends correctly", "[path-util]") {
    TestPathFixture fix;
    auto cfg = GetCfgPath("sub/dir");
    REQUIRE(cfg.find("sub/dir") != std::string::npos);
}

TEST_CASE("PathUtil: GetDbPath returns path containing db", "[path-util]") {
    TestPathFixture fix;
    auto db = GetDbPath();
    REQUIRE(db.find("db") != std::string::npos);
}

TEST_CASE("PathUtil: GetLogPath returns non-empty", "[path-util]") {
    TestPathFixture fix;
    auto logPath = GetLogPath();
    REQUIRE(!logPath.empty());
}

TEST_CASE("PathUtil: GetUploadPath returns non-empty", "[path-util]") {
    TestPathFixture fix;
    auto upload = GetUploadPath();
    REQUIRE(!upload.empty());
}

TEST_CASE("PathUtil: GetModelPath returns non-empty", "[path-util]") {
    TestPathFixture fix;
    auto model = GetModelPath();
    REQUIRE(!model.empty());
}

TEST_CASE("PathUtil: GetModelSearchPaths returns at least one entry", "[path-util]") {
    TestPathFixture fix;
    auto paths = GetModelSearchPaths();
    REQUIRE(!paths.empty());
}

TEST_CASE("PathUtil: GetResourcePath returns non-empty", "[path-util]") {
    TestPathFixture fix;
    auto res = GetResourcePath();
    REQUIRE(!res.empty());
}

TEST_CASE("PathUtil: GetEventPath creates directory", "[path-util]") {
    TestPathFixture fix;
    uint64_t ts    = 1700000000000;
    auto eventPath = GetEventPath(ts, true);
    REQUIRE(!eventPath.empty());
    REQUIRE(std::filesystem::exists(eventPath));
}

TEST_CASE("PathUtil: GetEventWebPath returns web-relative path", "[path-util]") {
    TestPathFixture fix;
    uint64_t ts  = 1700000000000;
    auto webPath = GetEventWebPath(ts);
    REQUIRE(!webPath.empty());
}

TEST_CASE("PathUtil: GetBaseDir returns test override path", "[path-util]") {
    TestPathFixture fix;
    auto base = GetBaseDir();
    REQUIRE(base.find(fix.test_dir) != std::string::npos);
}

TEST_CASE("PathUtil: GetBackupCfgPath returns non-empty", "[path-util]") {
    TestPathFixture fix;
    auto backup = GetBackupCfgPath();
    REQUIRE(!backup.empty());

    SECTION("With subPath") {
        auto sub = GetBackupCfgPath("test.json");
        REQUIRE(sub.find("test.json") != std::string::npos);
    }
}

TEST_CASE("PathUtil: GetUpgradePath returns non-empty", "[path-util]") {
    TestPathFixture fix;
    auto upgrade = GetUpgradePath();
    REQUIRE(!upgrade.empty());
}

TEST_CASE("PathUtil: GetFaceLibPhotoDir returns non-empty", "[path-util]") {
    TestPathFixture fix;
    auto dir = GetFaceLibPhotoDir();
    REQUIRE(!dir.empty());
}

TEST_CASE("PathUtil: GetTemporaryDirPath returns non-empty", "[path-util]") {
    TestPathFixture fix;
    auto tmp = GetTemporaryDirPath();
    REQUIRE(!tmp.empty());
}
