#include "catch_amalgamated.hpp"
/// @file test_passenger_flow_dao.cc
/// @brief PassengerFlowDao unit tests — validates CreateTable, AddNumber,
///        Query, QueryOrigin, Reported, and ResetDateNumber using in-memory SQLite.

#include <SQLiteCpp/SQLiteCpp.h>

#include "db/PassengerFlowDao.h"

namespace cosmo::test {

namespace {

    SQLite::Database MakeTestDb() {
        SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db::PassengerFlowDao dao(db);
        dao.CreateTable();
        return db;
    }

}  // namespace

TEST_CASE("PassengerFlowDao: CreateTable is idempotent", "[passenger-flow-dao]") {
    auto db = MakeTestDb();
    db::PassengerFlowDao dao(db);
    REQUIRE_NOTHROW(dao.CreateTable());
}

TEST_CASE("PassengerFlowDao: AddNumber inserts and accumulates", "[passenger-flow-dao]") {
    auto db = MakeTestDb();
    db::PassengerFlowDao dao(db);

    SECTION("first AddNumber creates a record") {
        REQUIRE(dao.AddNumber("cam-1", "pflow", 2024061710, 5, 3));

        db::PassengerFlowCondition cond;
        cond.camera_id      = "cam-1";
        cond.algorithm_code = "pflow";
        cond.type           = db::PassengerFlowConditionType::Hour;
        auto result         = dao.QueryOrigin(cond);
        REQUIRE(result.total_count == 1);
        REQUIRE(result.number_list[0].enter_number == 5);
        REQUIRE(result.number_list[0].leave_number == 3);
    }

    SECTION("second AddNumber accumulates on same hour") {
        REQUIRE(dao.AddNumber("cam-1", "pflow", 2024061710, 5, 3));
        REQUIRE(dao.AddNumber("cam-1", "pflow", 2024061710, 2, 1));

        db::PassengerFlowCondition cond;
        cond.camera_id      = "cam-1";
        cond.algorithm_code = "pflow";
        cond.type           = db::PassengerFlowConditionType::Hour;
        auto result         = dao.QueryOrigin(cond);
        REQUIRE(result.total_count == 1);
        REQUIRE(result.number_list[0].enter_number == 7);
        REQUIRE(result.number_list[0].leave_number == 4);
    }

    SECTION("different hours create separate records") {
        REQUIRE(dao.AddNumber("cam-1", "pflow", 2024061710, 5, 3));
        REQUIRE(dao.AddNumber("cam-1", "pflow", 2024061711, 2, 1));

        db::PassengerFlowCondition cond;
        cond.camera_id      = "cam-1";
        cond.algorithm_code = "pflow";
        cond.type           = db::PassengerFlowConditionType::Hour;
        auto result         = dao.QueryOrigin(cond);
        REQUIRE(result.total_count == 2);
    }
}

TEST_CASE("PassengerFlowDao: QueryOrigin filters by camera_id", "[passenger-flow-dao]") {
    auto db = MakeTestDb();
    db::PassengerFlowDao dao(db);

    REQUIRE(dao.AddNumber("cam-1", "pflow", 2024061710, 5, 3));
    REQUIRE(dao.AddNumber("cam-2", "pflow", 2024061710, 2, 1));

    db::PassengerFlowCondition cond;
    cond.camera_id      = "cam-1";
    cond.algorithm_code = "pflow";
    cond.type           = db::PassengerFlowConditionType::Hour;
    auto result         = dao.QueryOrigin(cond);
    REQUIRE(result.total_count == 1);
    REQUIRE(result.number_list[0].camera_id == "cam-1");
}

TEST_CASE("PassengerFlowDao: QueryOrigin filters by time range", "[passenger-flow-dao]") {
    auto db = MakeTestDb();
    db::PassengerFlowDao dao(db);

    REQUIRE(dao.AddNumber("cam-1", "pflow", 2024061708, 1, 0));
    REQUIRE(dao.AddNumber("cam-1", "pflow", 2024061710, 5, 3));
    REQUIRE(dao.AddNumber("cam-1", "pflow", 2024061712, 2, 1));

    db::PassengerFlowCondition cond;
    cond.camera_id      = "cam-1";
    cond.algorithm_code = "pflow";
    cond.type           = db::PassengerFlowConditionType::Hour;
    cond.start_hour     = 2024061709;
    cond.end_hour       = 2024061711;
    auto result         = dao.QueryOrigin(cond);
    REQUIRE(result.total_count == 1);
    REQUIRE(result.number_list[0].hour == 2024061710);
}

TEST_CASE("PassengerFlowDao: QueryOrigin ordering", "[passenger-flow-dao]") {
    auto db = MakeTestDb();
    db::PassengerFlowDao dao(db);

    REQUIRE(dao.AddNumber("cam-1", "pflow", 2024061708, 1, 0));
    REQUIRE(dao.AddNumber("cam-1", "pflow", 2024061710, 5, 3));
    REQUIRE(dao.AddNumber("cam-1", "pflow", 2024061712, 2, 1));

    db::PassengerFlowCondition cond;
    cond.camera_id      = "cam-1";
    cond.algorithm_code = "pflow";
    cond.type           = db::PassengerFlowConditionType::Hour;

    SECTION("ASC ordering") {
        auto result = dao.QueryOrigin(cond, 0, 1);
        REQUIRE(result.number_list[0].hour == 2024061708);
        REQUIRE(result.number_list[2].hour == 2024061712);
    }

    SECTION("DESC ordering") {
        auto result = dao.QueryOrigin(cond, 0, 0);
        REQUIRE(result.number_list[0].hour == 2024061712);
        REQUIRE(result.number_list[2].hour == 2024061708);
    }
}

TEST_CASE("PassengerFlowDao: QueryOrigin with max_size limit", "[passenger-flow-dao]") {
    auto db = MakeTestDb();
    db::PassengerFlowDao dao(db);

    REQUIRE(dao.AddNumber("cam-1", "pflow", 2024061708, 1, 0));
    REQUIRE(dao.AddNumber("cam-1", "pflow", 2024061710, 5, 3));
    REQUIRE(dao.AddNumber("cam-1", "pflow", 2024061712, 2, 1));

    db::PassengerFlowCondition cond;
    cond.camera_id      = "cam-1";
    cond.algorithm_code = "pflow";
    cond.type           = db::PassengerFlowConditionType::Hour;

    auto result = dao.QueryOrigin(cond, 2);
    REQUIRE(result.total_count == 3);
    REQUIRE(result.number_list.size() == 2);
}

TEST_CASE("PassengerFlowDao: Reported marks record as reported", "[passenger-flow-dao]") {
    auto db = MakeTestDb();
    db::PassengerFlowDao dao(db);

    REQUIRE(dao.AddNumber("cam-1", "pflow", 2024061710, 5, 3));
    REQUIRE(dao.Reported("cam-1", "pflow", 2024061710));

    db::PassengerFlowCondition cond;
    cond.camera_id      = "cam-1";
    cond.algorithm_code = "pflow";
    cond.reported       = 1;
    cond.type           = db::PassengerFlowConditionType::Hour;
    auto result         = dao.QueryOrigin(cond);
    REQUIRE(result.total_count == 1);
}

TEST_CASE("PassengerFlowDao: AddNumber resets isreported to 0", "[passenger-flow-dao]") {
    auto db = MakeTestDb();
    db::PassengerFlowDao dao(db);

    REQUIRE(dao.AddNumber("cam-1", "pflow", 2024061710, 5, 3));
    REQUIRE(dao.Reported("cam-1", "pflow", 2024061710));

    // Add more data — should reset isreported to 0
    REQUIRE(dao.AddNumber("cam-1", "pflow", 2024061710, 1, 0));

    db::PassengerFlowCondition cond;
    cond.camera_id      = "cam-1";
    cond.algorithm_code = "pflow";
    cond.reported       = 0;
    cond.type           = db::PassengerFlowConditionType::Hour;
    auto result         = dao.QueryOrigin(cond);
    REQUIRE(result.total_count == 1);
}

TEST_CASE("PassengerFlowDao: ResetDateNumber zeros out counts for a date", "[passenger-flow-dao]") {
    auto db = MakeTestDb();
    db::PassengerFlowDao dao(db);

    // Date 20240617: hours 2024061700-2024061723
    REQUIRE(dao.AddNumber("cam-1", "pflow", 2024061708, 5, 3));
    REQUIRE(dao.AddNumber("cam-1", "pflow", 2024061712, 2, 1));
    // Date 20240618
    REQUIRE(dao.AddNumber("cam-1", "pflow", 2024061810, 10, 7));

    REQUIRE(dao.ResetDateNumber("cam-1", "pflow", 20240617));

    db::PassengerFlowCondition cond;
    cond.camera_id      = "cam-1";
    cond.algorithm_code = "pflow";
    cond.type           = db::PassengerFlowConditionType::Hour;

    // All records for date 20240617 should have zeroed counts
    cond.start_hour = 2024061700;
    cond.end_hour   = 2024061800;
    auto result     = dao.QueryOrigin(cond);
    REQUIRE(result.total_count == 2);
    for (const auto& tp : result.number_list) {
        REQUIRE(tp.enter_number == 0);
        REQUIRE(tp.leave_number == 0);
    }

    // Date 20240618 should be unaffected
    cond.start_hour = 2024061800;
    cond.end_hour   = 2024061900;
    auto result2    = dao.QueryOrigin(cond);
    REQUIRE(result2.total_count == 1);
    REQUIRE(result2.number_list[0].enter_number == 10);
}

TEST_CASE("PassengerFlowDao: Query aggregates by hour division", "[passenger-flow-dao]") {
    auto db = MakeTestDb();
    db::PassengerFlowDao dao(db);

    // Insert two hourly records for same day: 2024061708 and 2024061709
    REQUIRE(dao.AddNumber("cam-1", "pflow", 2024061708, 5, 3));
    REQUIRE(dao.AddNumber("cam-1", "pflow", 2024061709, 2, 1));

    SECTION("Hour type returns each hour separately") {
        db::PassengerFlowCondition cond;
        cond.camera_id      = "cam-1";
        cond.algorithm_code = "pflow";
        cond.type           = db::PassengerFlowConditionType::Hour;
        auto result         = dao.Query(cond);
        REQUIRE(result.total_count == 2);
    }

    SECTION("Day type groups hours into days") {
        db::PassengerFlowCondition cond;
        cond.camera_id      = "cam-1";
        cond.algorithm_code = "pflow";
        cond.type           = db::PassengerFlowConditionType::Day;
        auto result         = dao.Query(cond);
        // Both hours belong to same day (20240617), so grouped into 1
        REQUIRE(result.total_count == 1);
        REQUIRE(result.number_list[0].enter_number == 7);
        REQUIRE(result.number_list[0].leave_number == 4);
    }
}

}  // namespace cosmo::test
