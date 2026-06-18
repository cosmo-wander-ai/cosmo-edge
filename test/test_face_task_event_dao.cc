#include "catch_amalgamated.hpp"
/// @file test_face_task_event_dao.cc
/// @brief FaceTaskEventDao unit tests — validates CreateTable, Insert, Query,
///        UpdateVideoFlag, and RemoveItems using an in-memory SQLite database.

#include <SQLiteCpp/SQLiteCpp.h>

#include "db/FaceTaskEventDao.h"

namespace cosmo::test {

namespace {

    SQLite::Database MakeTestDb() {
        SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db::FaceTaskEventDao dao(db);
        dao.CreateTable();
        return db;
    }

    db::FaceTaskEventData MakeEvent(const std::string& id, const std::string& camera_name = "cam-1",
                                    int64_t timestamp = 1000, const std::string& area_id = "area-1",
                                    const std::string& algorithm_code = "face-001",
                                    const std::string& category       = "1") {
        db::FaceTaskEventData ev;
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

TEST_CASE("FaceTaskEventDao: CreateTable is idempotent", "[face-task-event-dao]") {
    auto db = MakeTestDb();
    db::FaceTaskEventDao dao(db);
    REQUIRE_NOTHROW(dao.CreateTable());
}

TEST_CASE("FaceTaskEventDao: Insert and Query roundtrip", "[face-task-event-dao]") {
    auto db = MakeTestDb();
    db::FaceTaskEventDao dao(db);

    auto ev             = MakeEvent("fev-001", "Entrance", 8000, "area-gate", "face_recog", "3");
    ev.track_id         = "track-xyz";
    ev.detected_pic_url = "/pics/face.jpg";
    ev.full_pic_url     = "/pics/full_face.jpg";
    ev.video_flag       = 1;
    ev.tar_x            = 50;
    ev.tar_y            = 60;
    ev.tar_width        = 120;
    ev.tar_height       = 160;
    ev.event_frame      = 99;
    REQUIRE(dao.Insert(ev));

    db::QueryFaceTaskEventCondition cond;
    cond.id     = "fev-001";
    auto result = dao.Query(cond);

    REQUIRE(result.total_count == 1);
    REQUIRE(result.behavior_list.size() == 1);

    const auto& got = result.behavior_list[0];
    REQUIRE(got.id == "fev-001");
    REQUIRE(got.camera_name == "Entrance");
    REQUIRE(got.timestamp == 8000);
    REQUIRE(got.area_id == "area-gate");
    REQUIRE(got.algorithm_code == "face_recog");
    REQUIRE(got.track_id == "track-xyz");
    REQUIRE(got.detected_pic_url == "/pics/face.jpg");
    REQUIRE(got.video_flag == 1);
    REQUIRE(got.tar_x == 50);
    REQUIRE(got.tar_width == 120);
    REQUIRE(got.event_frame == 99);
}

TEST_CASE("FaceTaskEventDao: Query filters by time range", "[face-task-event-dao]") {
    auto db = MakeTestDb();
    db::FaceTaskEventDao dao(db);

    REQUIRE(dao.Insert(MakeEvent("f1", "cam-1", 1000)));
    REQUIRE(dao.Insert(MakeEvent("f2", "cam-1", 2000)));
    REQUIRE(dao.Insert(MakeEvent("f3", "cam-1", 3000)));

    db::QueryFaceTaskEventCondition cond;
    cond.time_begin = 1500;
    cond.time_end   = 2500;
    auto result     = dao.Query(cond);
    REQUIRE(result.total_count == 1);
    REQUIRE(result.behavior_list[0].id == "f2");
}

TEST_CASE("FaceTaskEventDao: Query filters by algorithm_code", "[face-task-event-dao]") {
    auto db = MakeTestDb();
    db::FaceTaskEventDao dao(db);

    REQUIRE(dao.Insert(MakeEvent("f1", "cam-1", 1000, "a", "face_detect")));
    REQUIRE(dao.Insert(MakeEvent("f2", "cam-1", 2000, "a", "face_recog")));

    db::QueryFaceTaskEventCondition cond;
    cond.algorithm_code = "face_detect";
    auto result         = dao.Query(cond);
    REQUIRE(result.total_count == 1);
    REQUIRE(result.behavior_list[0].id == "f1");
}

TEST_CASE("FaceTaskEventDao: Query ordering", "[face-task-event-dao]") {
    auto db = MakeTestDb();
    db::FaceTaskEventDao dao(db);

    REQUIRE(dao.Insert(MakeEvent("f1", "cam-1", 1000)));
    REQUIRE(dao.Insert(MakeEvent("f2", "cam-1", 3000)));
    REQUIRE(dao.Insert(MakeEvent("f3", "cam-1", 2000)));

    SECTION("DESC order") {
        db::QueryFaceTaskEventCondition cond;
        auto result = dao.Query(cond, 0);
        REQUIRE(result.behavior_list[0].timestamp == 3000);
        REQUIRE(result.behavior_list[2].timestamp == 1000);
    }

    SECTION("ASC order") {
        db::QueryFaceTaskEventCondition cond;
        auto result = dao.Query(cond, 1);
        REQUIRE(result.behavior_list[0].timestamp == 1000);
        REQUIRE(result.behavior_list[2].timestamp == 3000);
    }
}

TEST_CASE("FaceTaskEventDao: Query pagination", "[face-task-event-dao]") {
    auto db = MakeTestDb();
    db::FaceTaskEventDao dao(db);

    for (int i = 1; i <= 5; ++i) {
        REQUIRE(dao.Insert(MakeEvent("f" + std::to_string(i), "cam-1", i * 1000)));
    }

    db::QueryFaceTaskEventCondition cond;
    cond.page_num  = 1;
    cond.page_size = 2;
    auto result    = dao.Query(cond, 1);

    REQUIRE(result.total_count == 5);
    REQUIRE(result.behavior_list.size() == 2);
}

TEST_CASE("FaceTaskEventDao: UpdateVideoFlag", "[face-task-event-dao]") {
    auto db = MakeTestDb();
    db::FaceTaskEventDao dao(db);

    REQUIRE(dao.Insert(MakeEvent("f1")));

    SECTION("update video_flag to 1") {
        REQUIRE(dao.UpdateVideoFlag("f1", 1));

        db::QueryFaceTaskEventCondition cond;
        cond.id     = "f1";
        auto result = dao.Query(cond);
        REQUIRE(result.behavior_list[0].video_flag == 1);
    }

    SECTION("empty record_id returns false") {
        REQUIRE_FALSE(dao.UpdateVideoFlag("", 1));
    }

    SECTION("non-existent record_id returns false") {
        REQUIRE_FALSE(dao.UpdateVideoFlag("nonexistent", 1));
    }
}

TEST_CASE("FaceTaskEventDao: RemoveItems", "[face-task-event-dao]") {
    auto db = MakeTestDb();
    db::FaceTaskEventDao dao(db);

    REQUIRE(dao.Insert(MakeEvent("f1")));
    REQUIRE(dao.Insert(MakeEvent("f2")));
    REQUIRE(dao.Insert(MakeEvent("f3")));

    dao.RemoveItems({"f1", "f3"});

    db::QueryFaceTaskEventCondition cond;
    auto result = dao.Query(cond);
    REQUIRE(result.total_count == 1);
    REQUIRE(result.behavior_list[0].id == "f2");
}

TEST_CASE("FaceTaskEventDao: Query with is_export_total_count false", "[face-task-event-dao]") {
    auto db = MakeTestDb();
    db::FaceTaskEventDao dao(db);

    REQUIRE(dao.Insert(MakeEvent("f1")));

    db::QueryFaceTaskEventCondition cond;
    cond.is_export_total_count = false;
    auto result                = dao.Query(cond);
    REQUIRE(result.total_count == 0);
    REQUIRE(result.behavior_list.size() == 1);
}

}  // namespace cosmo::test
