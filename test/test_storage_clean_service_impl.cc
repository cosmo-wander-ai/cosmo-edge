#include "catch_amalgamated.hpp"
/*
 * test_storage_clean_service_impl.cc — StorageCleanServiceImpl unit tests (DEBT-T01)
 *
 * Strategy: Test lifecycle safety (Start/Stop idempotency).
 * Actual cleanup logic depends on StorageSpace and file system state,
 * so we only verify Start/Stop contract and crash safety.
 */
#include <chrono>
#include <filesystem>

#include "mock/MockAlarmRecordService.h"
#include "service/infra/impl/StorageCleanServiceImpl.h"
#include "support/ScopedPathOverride.h"
#include "support/ScopedServiceOverride.h"
#include "util/FileUtil.h"

using namespace cosmo::service;

TEST_CASE("StorageCleanServiceImpl: construction and destruction", "[StorageCleanService]") {
    REQUIRE_NOTHROW([]() { StorageCleanServiceImpl sut; }());
}

TEST_CASE("StorageCleanServiceImpl: Start then Stop lifecycle", "[StorageCleanService]") {
    const std::string root = "/tmp/cosmo_storage_clean_service_test";
    cosmo::test::ScopedPathOverride path_override(root, root);

    StorageCleanServiceImpl sut;

    SECTION("Start then Stop does not crash") {
        REQUIRE_NOTHROW(sut.Start());
        REQUIRE_NOTHROW(sut.Stop());
    }

    SECTION("Double stop is safe") {
        sut.Start();
        REQUIRE_NOTHROW(sut.Stop());
        REQUIRE_NOTHROW(sut.Stop());
    }

    SECTION("Stop without start is safe") {
        REQUIRE_NOTHROW(sut.Stop());
    }
}

TEST_CASE("StorageCleanServiceImpl: destructor calls Stop", "[StorageCleanService]") {
    const std::string root = "/tmp/cosmo_storage_clean_service_test";
    cosmo::test::ScopedPathOverride path_override(root, root);

    REQUIRE_NOTHROW([]() {
        StorageCleanServiceImpl sut;
        sut.Start();
        // destructor should call Stop
    }());
}

// Force the low-space policy on an isolated volume and protect indexed, recent, and active media.
TEST_CASE("StorageSpace reclaims aged unindexed media without deleting protected files",
          "[storage-recovery]") {
    namespace fs    = std::filesystem;
    const auto root = fs::temp_directory_path() / "cosmo-storage-recovery-test";
    fs::remove_all(root);
    fs::create_directories(root);
    cosmo::test::ScopedPathOverride paths(root.string(), root.string());
    const auto events = fs::path(cosmo::path::GetEventRootPath()) / "2026/09/24";
    fs::create_directories(events);
    const std::string orphan  = "00000000-0000-0000-0000-000000000001";
    const std::string indexed = "00000000-0000-0000-0000-000000000002";
    const std::string recent  = "00000000-0000-0000-0000-000000000003";
    const std::string active  = "00000000-0000-0000-0000-000000000004";
    for (const auto& id : {orphan, indexed, recent, active}) {
        const auto file = events / (id + "_full.jpg");
        REQUIRE(cosmo::util::WriteFile(file.string(), std::string(4096, 'x')));
        if (id != recent)
            fs::last_write_time(file, fs::file_time_type::clock::now() - std::chrono::hours(2));
    }
    REQUIRE(cosmo::util::WriteFile((events / (active + "_video.tmp")).string(), std::string("active")));
    fs::create_symlink(events / (indexed + "_full.jpg"), events / (orphan + "_orig.jpg"));
    cosmo::test::MockAlarmRecordService records;
    cosmo::test::ScopedServiceOverride<IAlarmRecordService> registration(records);
    ALLOW_CALL(records, QueryAlarmRecords(trompeloeil::_, 1)).RETURN(AlarmQueryResult{});
    ALLOW_CALL(records, HasStoredEvent(trompeloeil::_)).LR_RETURN(_1 == indexed);
    cosmo::StorageSpaceParam policy;
    policy.storage_reserve_space = fs::space(root).available + 1024 * 1024;
    cosmo::StorageSpace cleaner(cosmo::path::GetEventRootPath(), policy);
    SECTION("Only aged unindexed regular files are reclaimed") {
        REQUIRE_NOTHROW(cleaner.DoClean());
        CHECK_FALSE(fs::exists(events / (orphan + "_full.jpg")));
        CHECK(fs::is_symlink(events / (orphan + "_orig.jpg")));
    }
    SECTION("Database failure never authorizes orphan deletion") {
        ALLOW_CALL(records, HasStoredEvent(trompeloeil::_)).THROW(std::runtime_error("database unavailable"));
        REQUIRE_NOTHROW(cleaner.DoClean());
        CHECK(fs::exists(events / (orphan + "_full.jpg")));
    }
    CHECK(fs::exists(events / (indexed + "_full.jpg")));
    CHECK(fs::exists(events / (recent + "_full.jpg")));
    CHECK(fs::exists(events / (active + "_full.jpg")));
    CHECK(fs::exists(events / (active + "_video.tmp")));
    fs::remove_all(root);
}

// A bounded scan must advance past referenced files instead of starving later orphan files.
TEST_CASE("StorageSpace advances orphan scans past a full batch of indexed files", "[storage-recovery]") {
    namespace fs    = std::filesystem;
    const auto root = fs::temp_directory_path() / "cosmo-storage-cursor-test";
    fs::remove_all(root);
    fs::create_directories(root);
    cosmo::test::ScopedPathOverride paths(root.string(), root.string());
    const auto events = fs::path(cosmo::path::GetEventRootPath()) / "2026/09/24";
    fs::create_directories(events);
    for (int i = 0; i < 256; ++i) {
        const auto digits = std::to_string(i);
        const auto id     = "00000000-0000-0000-0000-" + std::string(12 - digits.size(), '0') + digits;
        const auto file   = events / (id + "_full.jpg");
        REQUIRE(cosmo::util::WriteFile(file.string(), std::string("x")));
        fs::last_write_time(file, fs::file_time_type::clock::now() - std::chrono::hours(2));
    }
    const std::string orphan = "ffffffff-ffff-ffff-ffff-ffffffffffff";
    const auto orphan_file   = events / (orphan + "_full.jpg");
    REQUIRE(cosmo::util::WriteFile(orphan_file.string(), std::string("x")));
    fs::last_write_time(orphan_file, fs::file_time_type::clock::now() - std::chrono::hours(2));
    cosmo::test::MockAlarmRecordService records;
    cosmo::test::ScopedServiceOverride<IAlarmRecordService> registration(records);
    ALLOW_CALL(records, QueryAlarmRecords(trompeloeil::_, 1)).RETURN(AlarmQueryResult{});
    ALLOW_CALL(records, HasStoredEvent(trompeloeil::_)).LR_RETURN(_1 != orphan);
    cosmo::StorageSpaceParam policy;
    policy.storage_reserve_space = fs::space(root).available + 1024 * 1024;
    cosmo::StorageSpace cleaner(cosmo::path::GetEventRootPath(), policy);
    cleaner.DoClean();
    CHECK(fs::exists(orphan_file));
    cleaner.DoClean();
    CHECK_FALSE(fs::exists(orphan_file));
    CHECK(std::distance(fs::directory_iterator(events), fs::directory_iterator()) == 256);
    fs::remove_all(root);
}
