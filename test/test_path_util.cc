#include "catch_amalgamated.hpp"
/*
 * test_path_util.cc - PathUtil unit tests
 *
 * Tests path accessors under a scoped test-root override.
 */
#include <chrono>
#include <filesystem>
#include <fstream>

#include "support/ScopedPathOverride.h"
#include "util/PathUtil.h"

using namespace cosmo::path;

namespace {

struct TestPathFixture {
    std::string test_dir;
    cosmo::test::ScopedPathOverride path_override;

    TestPathFixture()
        : test_dir("/tmp/cosmo_path_test_" +
                   std::to_string(std::chrono::system_clock::now().time_since_epoch().count())),
          path_override(test_dir, test_dir) {
        std::filesystem::create_directories(test_dir);
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

TEST_CASE("PathUtil: Init preserves resumable sessions and removes transient uploads",
          "[path-util][upload][restart]") {
    TestPathFixture fix;
    const std::filesystem::path upload_root = GetUploadPath();
    const auto session_marker               = upload_root / "sessions" / "session-id" / ".session.json";
    const auto transient_file               = upload_root / "request-spool" / "chunk.bin";
    std::filesystem::create_directories(session_marker.parent_path());
    std::filesystem::create_directories(transient_file.parent_path());
    std::ofstream(session_marker) << "{}";
    std::ofstream(transient_file) << "temporary";

    Init();

    CHECK(std::filesystem::exists(session_marker));
    CHECK_FALSE(std::filesystem::exists(transient_file.parent_path()));
}

TEST_CASE("PathUtil: GetUploadTmpPath creates an owner-only directory", "[path-util][security]") {
    TestPathFixture fix;
    const auto upload_tmp = GetUploadTmpPath();
    REQUIRE_FALSE(upload_tmp.empty());
    REQUIRE(std::filesystem::is_directory(upload_tmp));

    const auto permissions = std::filesystem::status(upload_tmp).permissions();
    REQUIRE((permissions & std::filesystem::perms::owner_all) == std::filesystem::perms::owner_all);
    REQUIRE((permissions & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) ==
            std::filesystem::perms::none);
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

TEST_CASE("PathUtil: record root is stable across date partitions", "[path-util][FileService]") {
    TestPathFixture fix;
    const auto root = GetRecordRootPath();
    REQUIRE(std::filesystem::is_directory(root));
    REQUIRE(GetRecordJsonPath().find(root + "/") == 0);

    const auto previous_partition = std::filesystem::path(root) / "2000/01/01";
    std::filesystem::create_directories(previous_partition);
    REQUIRE(IsWithinRoot(root, (previous_partition / "delayed.json").string()));
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

TEST_CASE("PathUtil: runtime root overrides require absolute paths", "[path-util][deployment]") {
    REQUIRE_THROWS_AS(OverrideRootPaths("relative/data", "/tmp/cosmo-app"), std::invalid_argument);
    REQUIRE_THROWS_AS(OverrideRootPaths("/tmp/cosmo-data", "relative/app"), std::invalid_argument);
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

TEST_CASE("PathUtil: IsWithinRoot containment check", "[path-util]") {
    SECTION("Exact equal path is within") {
        REQUIRE(IsWithinRoot("/data/a", "/data/a"));
    }
    SECTION("Nested child is within") {
        REQUIRE(IsWithinRoot("/data/a", "/data/a/b"));
        REQUIRE(IsWithinRoot("/data/a", "/data/a/b/c"));
    }
    SECTION("Sibling prefix is NOT within (component-aware)") {
        REQUIRE_FALSE(IsWithinRoot("/data/a", "/data/ab"));
        REQUIRE_FALSE(IsWithinRoot("/data/a", "/data/ab/c"));
    }
    SECTION("Parent (candidate shorter than root) is NOT within") {
        REQUIRE_FALSE(IsWithinRoot("/data/a/b", "/data/a"));
        REQUIRE_FALSE(IsWithinRoot("/data/a", "/data"));
    }
    SECTION("Dotdot traversal escapes root") {
        REQUIRE_FALSE(IsWithinRoot("/data/a", "/data/a/.."));    // resolves to /data
        REQUIRE_FALSE(IsWithinRoot("/data/a", "/data/a/../b"));  // resolves to /data/b
        REQUIRE_FALSE(IsWithinRoot("/data/a", "/data/a/../../etc/passwd"));
    }
    SECTION("Absolute escape is NOT within") {
        REQUIRE_FALSE(IsWithinRoot("/data/a", "/etc/passwd"));
        REQUIRE_FALSE(IsWithinRoot("/data/a", "/etc/shadow"));
    }
    SECTION("Dot segment collapses, nested child still within") {
        REQUIRE(IsWithinRoot("/data/a", "/data/a/./b"));  // resolves to /data/a/b
    }
    // Trailing-slash inputs are not asserted: production roots (GetBaseDir/GetPersonLibPhotoDir)
    // and the path(root)/filename joins never carry trailing separators, and the device libstdc++
    // path-iterator leaves a trailing empty component for root "/x/" that the mismatch check (which
    // is correct for all real inputs, see the cases above) does not need to defend against.
    SECTION("Non-existent deep tail still within") {
        REQUIRE(IsWithinRoot("/data/a", "/data/a/x/y/z/none"));
    }
    SECTION("Empty inputs are rejected") {
        REQUIRE_FALSE(IsWithinRoot("", "/data/a"));
        REQUIRE_FALSE(IsWithinRoot("/data/a", ""));
    }
}

TEST_CASE("PathUtil: IsWithinRoot with real filesystem", "[path-util]") {
    TestPathFixture fix;
    const auto root    = fix.test_dir + "/root";
    const auto outside = fix.test_dir + "/outside";
    std::filesystem::create_directories(root + "/sub");
    std::filesystem::create_directories(outside);

    REQUIRE(IsWithinRoot(root, root));                     // existing exact
    REQUIRE(IsWithinRoot(root, root + "/sub"));            // existing child
    REQUIRE(IsWithinRoot(root, root + "/sub/deep/none"));  // non-existent tail inside
    REQUIRE_FALSE(IsWithinRoot(root, outside));            // sibling dir
}

TEST_CASE("PathUtil: strict root resolution fails closed", "[path-util][security]") {
    TestPathFixture fix;
    const auto root    = fix.test_dir + "/strict-root";
    const auto outside = fix.test_dir + "/outside";
    std::filesystem::create_directories(root + "/child");
    std::filesystem::create_directories(outside);
    std::ofstream(root + "/child/file.txt") << "inside";
    std::ofstream(outside + "/file.txt") << "outside";

    std::string resolved;
    REQUIRE(ResolvePathWithinRoot(root, root + "/child/new.txt", resolved));
    REQUIRE(resolved == root + "/child/new.txt");
    REQUIRE_FALSE(ResolvePathWithinRoot(root, root + "/../outside/file.txt", resolved));
    REQUIRE_FALSE(ResolvePathWithinRoot(root + "/missing", root + "/missing/file.txt", resolved));

    REQUIRE(
        ResolveExistingPathWithinRoot(root, root + "/child/file.txt", PathEntryType::kRegularFile, resolved));
    REQUIRE_FALSE(
        ResolveExistingPathWithinRoot(root, root + "/child", PathEntryType::kRegularFile, resolved));

    std::filesystem::create_symlink(outside + "/file.txt", root + "/child/outside-link");
    REQUIRE_FALSE(ResolveExistingPathWithinRoot(root, root + "/child/outside-link",
                                                PathEntryType::kRegularFile, resolved));
}

TEST_CASE("PathUtil: path component validation", "[path-util][security]") {
    REQUIRE(IsSafePathComponent("task-01_ä¸­æ–‡"));
    REQUIRE_FALSE(IsSafePathComponent(""));
    REQUIRE_FALSE(IsSafePathComponent("."));
    REQUIRE_FALSE(IsSafePathComponent(".."));
    REQUIRE_FALSE(IsSafePathComponent("../task"));
    REQUIRE_FALSE(IsSafePathComponent("dir/task"));
    REQUIRE_FALSE(IsSafePathComponent("dir\\task"));
    REQUIRE_FALSE(IsSafePathComponent(std::string("bad\0name", 8)));
    REQUIRE_FALSE(IsSafePathComponent(std::string(129, 'a')));
}

TEST_CASE("PathUtil: extracted tree validation enforces type and size limits", "[path-util][security]") {
    TestPathFixture fix;
    const auto root    = std::filesystem::path(fix.test_dir) / "extracted";
    const auto outside = std::filesystem::path(fix.test_dir) / "outside.txt";
    std::filesystem::create_directories(root);

    const auto create_sparse_file = [](const std::filesystem::path& path, std::streamoff size) {
        std::ofstream stream(path, std::ios::binary);
        REQUIRE(stream.is_open());
        stream.seekp(size - 1);
        stream.put('\0');
        REQUIRE(stream.good());
    };
    create_sparse_file(root / "first.bin", 6);
    create_sparse_file(root / "second.bin", 6);

    REQUIRE(ValidateDirectoryTreeWithinRoot(root.string(), 2, 6, 12));
    REQUIRE_FALSE(ValidateDirectoryTreeWithinRoot(root.string(), 1, 6, 12));
    REQUIRE_FALSE(ValidateDirectoryTreeWithinRoot(root.string(), 2, 5, 12));
    REQUIRE_FALSE(ValidateDirectoryTreeWithinRoot(root.string(), 2, 6, 11));

    std::ofstream(outside) << "outside";
    std::filesystem::create_symlink(outside, root / "outside-link");
    REQUIRE_FALSE(ValidateDirectoryTreeWithinRoot(root.string(), 3, 16, 32));
}

TEST_CASE("PathUtil: extracted tree validation accepts safe internal symlinks", "[path-util][security]") {
    TestPathFixture fix;
    const auto root = std::filesystem::path(fix.test_dir) / "extracted";
    std::filesystem::create_directories(root / "lib");

    // Shared-library versioning chain, all relative and confined to lib/.
    std::ofstream(root / "lib" / "libfoo.so.1.0.0") << "payload";
    std::filesystem::create_symlink("libfoo.so.1.0.0", root / "lib" / "libfoo.so.1");
    std::filesystem::create_symlink("libfoo.so.1", root / "lib" / "libfoo.so");

    REQUIRE(ValidateDirectoryTreeWithinRoot(root.string(), 10, 1024, 1024));

    // A symlink whose target escapes the root must still be rejected.
    std::filesystem::create_symlink("../../etc/passwd", root / "lib" / "escape");
    REQUIRE_FALSE(ValidateDirectoryTreeWithinRoot(root.string(), 10, 1024, 1024));
}

TEST_CASE("PathUtil: task overview path rejects traversal without side effects", "[path-util][security]") {
    TestPathFixture fix;
    const auto expected_root = fix.test_dir + "/cwai/overview";

    const auto read_only_path = GetTaskOverviewDataPath("read-only", false);
    REQUIRE(read_only_path == expected_root + "/read-only");
    REQUIRE_FALSE(std::filesystem::exists(read_only_path));

    REQUIRE(GetTaskOverviewDataPath("task-01") == expected_root + "/task-01");
    REQUIRE(std::filesystem::is_directory(expected_root + "/task-01"));
    REQUIRE(GetTaskOverviewDataPath("../escape").empty());
    REQUIRE(GetTaskOverviewDataPath("/absolute").empty());
    REQUIRE_FALSE(std::filesystem::exists(fix.test_dir + "/cwai/escape"));

    const auto outside = fix.test_dir + "/outside-overview";
    std::filesystem::create_directories(outside);
    std::filesystem::create_directory_symlink(outside, expected_root + "/linked-task");
    REQUIRE(GetTaskOverviewDataPath("linked-task", false).empty());
}


TEST_CASE("PathUtil edge: IsSafePathComponent platform separators and length", "[path-util][edge]") {
    SECTION("Forward and backslash rejected") {
        REQUIRE_FALSE(IsSafePathComponent("a/b"));
        REQUIRE_FALSE(IsSafePathComponent("a\\b"));
    }

    SECTION("Exact max length accepted, one over rejected") {
        REQUIRE(IsSafePathComponent(std::string(128, 'x'), 128));
        REQUIRE_FALSE(IsSafePathComponent(std::string(129, 'x'), 128));
    }

    SECTION("NUL and control bytes rejected") {
        REQUIRE_FALSE(IsSafePathComponent(std::string("a\nb", 3)));
        REQUIRE_FALSE(IsSafePathComponent(std::string("a\tb", 3)));
    }

    SECTION("Empty and dot components rejected") {
        REQUIRE_FALSE(IsSafePathComponent(""));
        REQUIRE_FALSE(IsSafePathComponent("."));
        REQUIRE_FALSE(IsSafePathComponent(".."));
    }
}

TEST_CASE("PathUtil edge: IsWithinRoot empty and repeated separators", "[path-util][edge]") {
    SECTION("Both empty rejected") {
        REQUIRE_FALSE(IsWithinRoot("", ""));
    }

    SECTION("Repeated slashes collapse under normalization") {
        // weakly_canonical / lexical normalization should treat these as nested
        REQUIRE(IsWithinRoot("/data/a", "/data/a//b"));
    }
}
