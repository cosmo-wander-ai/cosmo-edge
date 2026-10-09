#include "catch_amalgamated.hpp"
/*
 * test_storage_clean_service_impl.cc — StorageCleanServiceImpl unit tests (DEBT-T01)
 *
 * Strategy: Test lifecycle safety (Start/Stop idempotency).
 * Actual cleanup logic depends on StorageSpace and file system state,
 * so we only verify Start/Stop contract and crash safety.
 */
#include <SQLiteCpp/SQLiteCpp.h>

#include <filesystem>
#include <fstream>

#include "db/TaskEventDao.h"
#include "mock/MockAlarmRecordService.h"
#include "service/infra/impl/StorageCleanServiceImpl.h"
#include "support/ScopedPathOverride.h"
#include "support/ScopedServiceOverride.h"
#include "util/UuidUtil.h"

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

TEST_CASE("Storage cleanup defers a locked delete and removes files only after retry succeeds",
          "[StorageCleanService][storage-lock]") {
    namespace fs = std::filesystem;
    struct Directory {
        fs::path path = fs::temp_directory_path() / ("storage-lock-" + cosmo::util::GenerateUUID());
        Directory() {
            fs::create_directories(path);
        }
        ~Directory() {
            std::error_code error;
            fs::remove_all(path, error);
        }
    } directory;
    cosmo::test::ScopedPathOverride paths(directory.path.string(), directory.path.string());
    const auto dbPath = (directory.path / "alarms.db").string();
    SQLite::Database database(dbPath, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    database.exec("PRAGMA journal_mode=WAL");
    database.setBusyTimeout(10);
    cosmo::db::TaskEventDao events(database);
    events.CreateTable();
    database.exec("INSERT INTO t_commonEvent(rec_id) VALUES('retained')");

    AlarmQueryResult result;
    AlarmEventRecord event;
    event.id         = "retained";
    event.timestamp  = 1700000000000;
    event.extraFiles = "[\"_test.jpg\"]";
    result.behaviorList.push_back(event);
    const fs::path eventPath = cosmo::path::GetEventPath(event.timestamp, false);
    fs::create_directories(eventPath);
    const auto image = eventPath / "retained_test.jpg";
    std::ofstream(image) << "kept until the database deletion succeeds";

    cosmo::test::MockAlarmRecordService alarms;
    cosmo::test::ScopedServiceOverride<IAlarmRecordService> registration(alarms);
    REQUIRE_CALL(alarms, QueryAlarmRecords(trompeloeil::_, 1)).TIMES(2).RETURN(result);
    REQUIRE_CALL(alarms, RemoveItems(std::vector<std::string>{"retained"}))
        .TIMES(2)
        .LR_SIDE_EFFECT(events.RemoveItems(_1));
    cosmo::StorageSpace cleanup(directory.path.string(),
                                {fs::space(directory.path).available + 64 * 1024 * 1024});
    SQLite::Database blocker(dbPath, SQLite::OPEN_READWRITE);
    blocker.exec("BEGIN IMMEDIATE");
    REQUIRE_NOTHROW(cleanup.DoClean());
    REQUIRE(database.execAndGet("SELECT COUNT(*) FROM t_commonEvent").getInt() == 1);
    REQUIRE(fs::exists(image));

    blocker.exec("ROLLBACK");
    REQUIRE_NOTHROW(cleanup.DoClean());
    REQUIRE(database.execAndGet("SELECT COUNT(*) FROM t_commonEvent").getInt() == 0);
    REQUIRE_FALSE(fs::exists(image));
}
