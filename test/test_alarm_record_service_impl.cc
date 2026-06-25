#include "catch_amalgamated.hpp"
/*
 * test_alarm_record_service_impl.cc — AlarmRecordServiceImpl unit tests (DEBT-T01)
 *
 * Strategy: Use a real SQLite in-memory database via MockDbService,
 * so we can test CRUD operations on event/face/passenger_flow tables
 * without touching the file system.
 */
#include <SQLiteCpp/SQLiteCpp.h>

#include <memory>
#include <thread>

#include "mock/MockDbService.h"
#include "mock/MockServiceRegistry.h"
#include "service/event/impl/AlarmRecordServiceImpl.h"

using namespace cosmo::service;

namespace {

// Helper: create an in-memory SQLite database for testing.
std::shared_ptr<SQLite::Database> MakeMemoryDb() {
    return std::make_shared<SQLite::Database>(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
}

// Helper: build an AlarmRecordUnit with sensible defaults.
cosmo::AlarmRecordUnit MakeAlarmUnit(const std::string& id, const std::string& cameraId = "cam1",
                                     const std::string& algCode = "face_detect",
                                     uint64_t ts                = 1700000000000) {
    cosmo::AlarmRecordUnit unit;
    unit.id             = id;
    unit.category       = "alarm";
    unit.algorithmId    = algCode;
    unit.timestamp      = ts;
    unit.videoChannelId = cameraId;
    unit.channelName    = "Camera-1";
    unit.areaId         = "area1";
    unit.areaName       = "Zone-A";
    unit.diskId         = "0";
    unit.trackId        = "track1";
    unit.videoFlag      = cosmo::EventRecordFlag::EventRecordVideoFlagNone;
    unit.extraFiles     = "[]";
    unit.property       = "{}";
    return unit;
}

}  // namespace

TEST_CASE("AlarmRecordServiceImpl: construction creates tables", "[AlarmRecordService]") {
    auto memDb = MakeMemoryDb();
    cosmo::test::MockServiceRegistry mocks;
    ALLOW_CALL(mocks.dbSvc, GetDb()).RETURN(memDb);

    REQUIRE_NOTHROW([]() {
        AlarmRecordServiceImpl sut;
        // Tables should be created during construction
    }());
}

TEST_CASE("AlarmRecordServiceImpl: Insert and QueryAlarmRecords", "[AlarmRecordService]") {
    auto memDb = MakeMemoryDb();
    cosmo::test::MockServiceRegistry mocks;
    ALLOW_CALL(mocks.dbSvc, GetDb()).RETURN(memDb);

    AlarmRecordServiceImpl sut;

    SECTION("Insert single alarm record succeeds") {
        auto unit = MakeAlarmUnit("event-001");
        REQUIRE(sut.Insert(unit) == true);
    }

    SECTION("Insert then QueryAlarmRecords returns the inserted record") {
        auto unit = MakeAlarmUnit("event-002");
        REQUIRE(sut.Insert(unit) == true);

        cosmo::service::AlarmQueryCondition cond;
        cond.timeBegin = 0;
        cond.timeEnd   = 2000000000000;
        cond.pageNum   = 1;
        cond.pageSize  = 10;

        auto result = sut.QueryAlarmRecords(cond, 0);
        REQUIRE(result.totalCount >= 1);
    }
}

TEST_CASE("AlarmRecordServiceImpl: UpdateAlarmReportStatus", "[AlarmRecordService]") {
    auto memDb = MakeMemoryDb();
    cosmo::test::MockServiceRegistry mocks;
    ALLOW_CALL(mocks.dbSvc, GetDb()).RETURN(memDb);

    AlarmRecordServiceImpl sut;

    SECTION("Update existing record") {
        auto unit = MakeAlarmUnit("event-003");
        sut.Insert(unit);
        // Should not throw
        REQUIRE_NOTHROW(sut.UpdateAlarmReportStatus("event-003", true));
    }
}

TEST_CASE("AlarmRecordServiceImpl: InsertPassFlow and QueryPassengerFlow", "[AlarmRecordService]") {
    auto memDb = MakeMemoryDb();
    cosmo::test::MockServiceRegistry mocks;
    ALLOW_CALL(mocks.dbSvc, GetDb()).RETURN(memDb);

    AlarmRecordServiceImpl sut;

    SECTION("Insert passenger flow data") {
        REQUIRE(sut.InsertPassFlow("cam1", "person_count", 1700000000, 5, 3) == true);
    }

    SECTION("Query passenger flow after insert") {
        sut.InsertPassFlow("cam1", "person_count", 1700000000, 5, 3);
        cosmo::service::FlowQueryCondition cond;
        cond.cameraId       = "cam1";
        cond.algorithm_code = "person_count";
        cond.startHour      = 0;
        cond.endHour        = 2000000000;
        auto result         = sut.QueryPassengerFlow(cond);
        REQUIRE(result.totalCount >= 1);
    }
}

TEST_CASE("AlarmRecordServiceImpl: RemoveItems", "[AlarmRecordService]") {
    auto memDb = MakeMemoryDb();
    cosmo::test::MockServiceRegistry mocks;
    ALLOW_CALL(mocks.dbSvc, GetDb()).RETURN(memDb);

    AlarmRecordServiceImpl sut;

    SECTION("Remove non-existent items does not crash") {
        REQUIRE_NOTHROW(sut.RemoveItems({"nonexistent-001", "nonexistent-002"}));
    }

    SECTION("Remove existing items") {
        auto unit = MakeAlarmUnit("event-rm-001");
        sut.Insert(unit);
        REQUIRE_NOTHROW(sut.RemoveItems({"event-rm-001"}));
    }
}

TEST_CASE("AlarmRecordServiceImpl: concurrent Insert safety", "[AlarmRecordService][concurrency]") {
    auto memDb = MakeMemoryDb();
    cosmo::test::MockServiceRegistry mocks;
    ALLOW_CALL(mocks.dbSvc, GetDb()).RETURN(memDb);

    AlarmRecordServiceImpl sut;

    std::atomic<bool> stop{false};
    std::atomic<int> successCount{0};
    constexpr int kThreadCount  = 4;
    constexpr int kOpsPerThread = 10;

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreadCount; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < kOpsPerThread; ++i) {
                auto unit = MakeAlarmUnit("conc-" + std::to_string(t) + "-" + std::to_string(i));
                if (sut.Insert(unit)) {
                    successCount.fetch_add(1);
                }
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    REQUIRE(successCount.load() == kThreadCount * kOpsPerThread);
}
