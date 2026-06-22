#include "catch_amalgamated.hpp"
/// @file test_task_event_dao.cc
/// @brief TaskEventDao unit tests — validates CreateTable, Insert, Query
///        (with conditions/pagination/ordering), UpdateRecordReportStatus,
///        and RemoveItems using an in-memory SQLite database.

#include <SQLiteCpp/SQLiteCpp.h>

#include "db/TaskEventDao.h"

namespace cosmo::test {

namespace {

    SQLite::Database MakeTestDb() {
        SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db::TaskEventDao dao(db);
        dao.CreateTable();
        return db;
    }

    db::TaskEventData MakeEvent(const std::string& id, const std::string& camera_name = "cam-1",
                                int64_t timestamp = 1000, const std::string& area_id = "area-1",
                                const std::string& algorithm_code = "alg-001",
                                const std::string& category       = "1") {
        db::TaskEventData ev;
        ev.id             = id;
        ev.camera_name    = camera_name;
        ev.timestamp      = timestamp;
        ev.area_id        = area_id;
        ev.algorithm_code = algorithm_code;
        ev.category       = category;
        ev.camera_id      = "cam-id-1";
        ev.report_status  = 0;
        return ev;
    }

}  // namespace

TEST_CASE("TaskEventDao: CreateTable is idempotent", "[task-event-dao]") {
    auto db = MakeTestDb();
    db::TaskEventDao dao(db);

    // Calling CreateTable again should not throw
    REQUIRE_NOTHROW(dao.CreateTable());
}

TEST_CASE("TaskEventDao: Insert and Query basic roundtrip", "[task-event-dao]") {
    auto db = MakeTestDb();
    db::TaskEventDao dao(db);

    auto ev             = MakeEvent("evt-001", "Front Door", 5000, "area-lobby", "face_detect", "2");
    ev.track_id         = "track-abc";
    ev.detected_pic_url = "/pics/detected.jpg";
    ev.full_pic_url     = "/pics/full.jpg";
    ev.orig_pic_url     = "/pics/orig.jpg";
    ev.lib_person_name  = "Alice";
    ev.prop_color       = "red";
    ev.tar_x            = 10;
    ev.tar_y            = 20;
    ev.tar_width        = 100;
    ev.tar_height       = 200;
    ev.event_frame      = 42;
    REQUIRE(dao.Insert(ev));

    db::QueryTaskEventCondition cond;
    cond.id     = "evt-001";
    auto result = dao.Query(cond);

    REQUIRE(result.total_count == 1);
    REQUIRE(result.behavior_list.size() == 1);

    const auto& got = result.behavior_list[0];
    REQUIRE(got.id == "evt-001");
    REQUIRE(got.camera_name == "Front Door");
    REQUIRE(got.timestamp == 5000);
    REQUIRE(got.area_id == "area-lobby");
    REQUIRE(got.algorithm_code == "face_detect");
    REQUIRE(got.track_id == "track-abc");
    REQUIRE(got.detected_pic_url == "/pics/detected.jpg");
    REQUIRE(got.lib_person_name == "Alice");
    REQUIRE(got.prop_color == "red");
    REQUIRE(got.tar_x == 10);
    REQUIRE(got.tar_y == 20);
    REQUIRE(got.tar_width == 100);
    REQUIRE(got.tar_height == 200);
    REQUIRE(got.event_frame == 42);
}

TEST_CASE("TaskEventDao: Query with no conditions returns all", "[task-event-dao]") {
    auto db = MakeTestDb();
    db::TaskEventDao dao(db);

    REQUIRE(dao.Insert(MakeEvent("e1", "cam-1", 1000)));
    REQUIRE(dao.Insert(MakeEvent("e2", "cam-2", 2000)));
    REQUIRE(dao.Insert(MakeEvent("e3", "cam-3", 3000)));

    db::QueryTaskEventCondition cond;
    auto result = dao.Query(cond);
    REQUIRE(result.total_count == 3);
    REQUIRE(result.behavior_list.size() == 3);
}

TEST_CASE("TaskEventDao: Query filters by area_id", "[task-event-dao]") {
    auto db = MakeTestDb();
    db::TaskEventDao dao(db);

    REQUIRE(dao.Insert(MakeEvent("e1", "cam-1", 1000, "area-A")));
    REQUIRE(dao.Insert(MakeEvent("e2", "cam-2", 2000, "area-B")));

    db::QueryTaskEventCondition cond;
    cond.area_id = "area-A";
    auto result  = dao.Query(cond);
    REQUIRE(result.total_count == 1);
    REQUIRE(result.behavior_list[0].id == "e1");
}

TEST_CASE("TaskEventDao: Query filters by time range", "[task-event-dao]") {
    auto db = MakeTestDb();
    db::TaskEventDao dao(db);

    REQUIRE(dao.Insert(MakeEvent("e1", "cam-1", 1000)));
    REQUIRE(dao.Insert(MakeEvent("e2", "cam-1", 2000)));
    REQUIRE(dao.Insert(MakeEvent("e3", "cam-1", 3000)));

    db::QueryTaskEventCondition cond;
    cond.time_begin = 1500;
    cond.time_end   = 2500;
    auto result     = dao.Query(cond);
    REQUIRE(result.total_count == 1);
    REQUIRE(result.behavior_list[0].id == "e2");
}

TEST_CASE("TaskEventDao: Query filters by algorithm_codes (IN clause)", "[task-event-dao]") {
    auto db = MakeTestDb();
    db::TaskEventDao dao(db);

    REQUIRE(dao.Insert(MakeEvent("e1", "cam-1", 1000, "a", "alg-A")));
    REQUIRE(dao.Insert(MakeEvent("e2", "cam-1", 2000, "a", "alg-B")));
    REQUIRE(dao.Insert(MakeEvent("e3", "cam-1", 3000, "a", "alg-C")));

    db::QueryTaskEventCondition cond;
    cond.algorithm_codes = {"alg-A", "alg-C"};
    auto result          = dao.Query(cond);
    REQUIRE(result.total_count == 2);
}

TEST_CASE("TaskEventDao: Query ordering", "[task-event-dao]") {
    auto db = MakeTestDb();
    db::TaskEventDao dao(db);

    REQUIRE(dao.Insert(MakeEvent("e1", "cam-1", 1000)));
    REQUIRE(dao.Insert(MakeEvent("e2", "cam-1", 3000)));
    REQUIRE(dao.Insert(MakeEvent("e3", "cam-1", 2000)));

    SECTION("default order is DESC") {
        db::QueryTaskEventCondition cond;
        auto result = dao.Query(cond, 0);  // 0 = DESC
        REQUIRE(result.behavior_list[0].timestamp == 3000);
        REQUIRE(result.behavior_list[1].timestamp == 2000);
        REQUIRE(result.behavior_list[2].timestamp == 1000);
    }

    SECTION("order=1 is ASC") {
        db::QueryTaskEventCondition cond;
        auto result = dao.Query(cond, 1);  // 1 = ASC
        REQUIRE(result.behavior_list[0].timestamp == 1000);
        REQUIRE(result.behavior_list[1].timestamp == 2000);
        REQUIRE(result.behavior_list[2].timestamp == 3000);
    }
}

TEST_CASE("TaskEventDao: Query pagination", "[task-event-dao]") {
    auto db = MakeTestDb();
    db::TaskEventDao dao(db);

    for (int i = 1; i <= 5; ++i) {
        REQUIRE(dao.Insert(MakeEvent("e" + std::to_string(i), "cam-1", i * 1000)));
    }

    db::QueryTaskEventCondition cond;
    cond.page_num  = 1;
    cond.page_size = 2;
    auto result    = dao.Query(cond, 1);  // ASC order

    REQUIRE(result.total_count == 5);
    REQUIRE(result.behavior_list.size() == 2);
    REQUIRE(result.behavior_list[0].timestamp == 1000);
    REQUIRE(result.behavior_list[1].timestamp == 2000);

    // Page 2
    cond.page_num = 2;
    result        = dao.Query(cond, 1);
    REQUIRE(result.behavior_list.size() == 2);
    REQUIRE(result.behavior_list[0].timestamp == 3000);
}

TEST_CASE("TaskEventDao: Query with is_export_total_count false skips count", "[task-event-dao]") {
    auto db = MakeTestDb();
    db::TaskEventDao dao(db);

    REQUIRE(dao.Insert(MakeEvent("e1")));

    db::QueryTaskEventCondition cond;
    cond.is_export_total_count = false;
    auto result                = dao.Query(cond);
    REQUIRE(result.total_count == 0);  // Count not computed
    REQUIRE(result.behavior_list.size() == 1);
}

TEST_CASE("TaskEventDao: UpdateRecordReportStatus", "[task-event-dao]") {
    auto db = MakeTestDb();
    db::TaskEventDao dao(db);

    REQUIRE(dao.Insert(MakeEvent("e1")));

    SECTION("mark as reported") {
        REQUIRE(dao.UpdateRecordReportStatus("e1", true));

        db::QueryTaskEventCondition cond;
        cond.report_status = 1;
        auto result        = dao.Query(cond);
        REQUIRE(result.total_count == 1);
        REQUIRE(result.behavior_list[0].report_status == 1);
    }

    SECTION("mark as unreported") {
        // First mark as reported, then unmark
        REQUIRE(dao.UpdateRecordReportStatus("e1", true));
        REQUIRE(dao.UpdateRecordReportStatus("e1", false));

        db::QueryTaskEventCondition cond;
        cond.report_status = 0;
        auto result        = dao.Query(cond);
        REQUIRE(result.total_count == 1);
    }

    SECTION("update non-existent record returns false") {
        REQUIRE_FALSE(dao.UpdateRecordReportStatus("nonexistent", true));
    }
}

TEST_CASE("TaskEventDao: RemoveItems deletes events", "[task-event-dao]") {
    auto db = MakeTestDb();
    db::TaskEventDao dao(db);

    REQUIRE(dao.Insert(MakeEvent("e1")));
    REQUIRE(dao.Insert(MakeEvent("e2")));
    REQUIRE(dao.Insert(MakeEvent("e3")));

    dao.RemoveItems({"e1", "e3"});

    db::QueryTaskEventCondition cond;
    auto result = dao.Query(cond);
    REQUIRE(result.total_count == 1);
    REQUIRE(result.behavior_list[0].id == "e2");
}

TEST_CASE("TaskEventDao: Query filters by report_status", "[task-event-dao]") {
    auto db = MakeTestDb();
    db::TaskEventDao dao(db);

    REQUIRE(dao.Insert(MakeEvent("e1")));
    REQUIRE(dao.Insert(MakeEvent("e2")));
    REQUIRE(dao.UpdateRecordReportStatus("e1", true));

    SECTION("filter reported only") {
        db::QueryTaskEventCondition cond;
        cond.report_status = 1;
        auto result        = dao.Query(cond);
        REQUIRE(result.total_count == 1);
        REQUIRE(result.behavior_list[0].id == "e1");
    }

    SECTION("filter unreported only") {
        db::QueryTaskEventCondition cond;
        cond.report_status = 0;
        auto result        = dao.Query(cond);
        REQUIRE(result.total_count == 1);
        REQUIRE(result.behavior_list[0].id == "e2");
    }

    SECTION("report_status -1 returns all") {
        db::QueryTaskEventCondition cond;
        cond.report_status = -1;
        auto result        = dao.Query(cond);
        REQUIRE(result.total_count == 2);
    }
}

}  // namespace cosmo::test
