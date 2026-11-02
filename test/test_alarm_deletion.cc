#include <SQLiteCpp/SQLiteCpp.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <memory>

#include "catch_amalgamated.hpp"
#include "mock/MockDbService.h"
#include "service/event/AlarmMediaActivity.h"
#include "service/event/impl/AlarmRecordServiceImpl.h"
#include "support/ScopedPathOverride.h"
#include "support/ScopedServiceOverride.h"
#include "util/PathUtil.h"
#include "util/UuidUtil.h"

namespace {
namespace fs = std::filesystem;
using namespace cosmo::service;

struct Environment {
    std::string root = "/tmp/alarm-delete-" + cosmo::util::GenerateUUID();
    cosmo::test::ScopedPathOverride paths{root, root};
    std::shared_ptr<SQLite::Database> db =
        std::make_shared<SQLite::Database>(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    cosmo::test::MockDbService dbService;
    cosmo::test::ScopedServiceOverride<IDbService> registration{dbService};
    std::unique_ptr<AlarmRecordServiceImpl> service;

    Environment() {
        fs::create_directories(root);
        ALLOW_CALL(dbService, GetDb()).RETURN(db);
        service = std::make_unique<AlarmRecordServiceImpl>();
    }
    ~Environment() {
        service.reset();
        std::error_code error;
        fs::remove_all(root, error);
    }

    void Insert(const std::string& id, const std::string& category = "1", const std::string& extra = "[]") {
        cosmo::AlarmRecordUnit event;
        event.id         = id;
        event.category   = category;
        event.timestamp  = 1700000000000;
        event.extraFiles = extra;
        event.property   = "{}";
        REQUIRE(service->Insert(event));
    }
    bool Exists(const std::string& id) {
        AlarmQueryCondition condition;
        condition.id = id;
        return !service->QueryAlarmRecords(condition, 0).behaviorList.empty();
    }
    fs::path Event(const std::string& date, const std::string& name) {
        return fs::path(cosmo::path::GetEventRootPath(false)) / date / name;
    }
    fs::path Feature(const std::string& date, const std::string& name) {
        return fs::path(cosmo::path::GetRecordRootPath(false)) / date / name;
    }
    void Write(const fs::path& file) {
        fs::create_directories(file.parent_path());
        std::ofstream output(file);
        output << "isolated test attachment";
        REQUIRE(output.good());
    }
};

}  // namespace

TEST_CASE("Alarm deletion removes exact event attachments across both date trees", "[alarm-delete]") {
    Environment env;
    env.Insert("face-a", "1", R"(["_full.jpg","_detect.jpg"])");
    env.Insert("body-b", "2");
    env.Insert("vehicle-c", "10");
    env.Insert("untouched", "1");
    const auto full         = env.Event("2022/12/31", "face-a_full.jpg");
    const auto detect       = env.Event("2022/12/31", "face-a_detect.jpg");
    const auto video        = env.Event("2023/01/01", "face-a_video.mp4");
    const auto tmp          = env.Event("2023/01/01", "face-a_video.tmp");
    const auto overview     = env.Event("2023/01/01", "face-a_overview.json");
    const auto json         = env.Event("2023/01/01", "face-a.json");
    const auto oldFeature   = env.Feature("2019/01/02", "face-a.feature");
    const auto newFeature   = env.Feature("2024/07/09", "face-a.feature");
    const auto bodyFeature  = env.Feature("2021/01/01", "body-b.feature");
    const auto otherFeature = env.Feature("2021/01/01", "face-a-other.feature");
    const auto otherEvent   = env.Event("2022/12/31", "untouched_full.jpg");
    for (const auto& file : {full, detect, video, tmp, overview, json, oldFeature, newFeature, bodyFeature,
                             otherFeature, otherEvent})
        env.Write(file);

    const auto result = env.service->DeleteEvents({"face-a", "body-b", "vehicle-c", "missing"});
    CHECK((result.deletedIds == std::vector<std::string>{"face-a", "body-b", "vehicle-c"}));
    CHECK(result.missingIds == std::vector<std::string>{"missing"});
    CHECK(result.failedItems.empty());
    for (const auto& file : {full, detect, video, tmp, overview, json, oldFeature, newFeature, bodyFeature})
        CHECK_FALSE(fs::exists(file));
    CHECK(fs::exists(otherFeature));
    CHECK(fs::exists(otherEvent));
    CHECK_FALSE(env.Exists("face-a"));
    CHECK_FALSE(env.Exists("body-b"));
    CHECK_FALSE(env.Exists("vehicle-c"));
    CHECK(env.Exists("untouched"));
}

TEST_CASE("Alarm deletion handles recording, unsupported and missing records independently",
          "[alarm-delete]") {
    Environment env;
    env.Insert("busy");
    env.Insert("ready", "3");
    env.Insert("statistics", "8");
    const auto feature = env.Feature("2022/11/01", "busy.feature");
    env.Write(feature);
    auto recording = AlarmMediaLease::StartRecording("busy");
    REQUIRE(recording);

    const auto first = env.service->DeleteEvents({"busy", "ready", "statistics", "missing"});
    CHECK(first.deletedIds == std::vector<std::string>{"ready"});
    REQUIRE(first.failedItems.size() == 2);
    CHECK(first.failedItems[0].reason == "RECORDING");
    CHECK(first.failedItems[1].reason == "UNSUPPORTED_CATEGORY");
    CHECK(env.Exists("busy"));
    CHECK(env.Exists("statistics"));
    CHECK(fs::exists(feature));
    recording.reset();
    const auto second = env.service->DeleteEvents({"busy"});
    CHECK(second.deletedIds == std::vector<std::string>{"busy"});
    CHECK_FALSE(fs::exists(feature));
    CHECK_FALSE(env.Exists("busy"));
    CHECK(env.service->DeleteEvents({"busy"}).missingIds == std::vector<std::string>{"busy"});
}

TEST_CASE("Alarm deletion preserves records and unrelated files on unsafe attachments", "[alarm-delete]") {
    Environment env;
    env.Insert("bad-list", "1", R"(["../outside"])");
    env.Insert("linked", "1", R"(["_full.jpg"])");
    const auto image   = env.Event("2022/01/01", "linked_full.jpg");
    const auto outside = fs::path(env.root) / "outside.feature";
    const auto feature = env.Feature("2022/01/01", "linked.feature");
    env.Write(image);
    env.Write(outside);
    fs::create_directories(feature.parent_path());
    fs::create_symlink(outside, feature);

    const auto result = env.service->DeleteEvents({"bad-list", "linked"});
    REQUIRE(result.failedItems.size() == 2);
    CHECK(result.failedItems[0].reason == "INVALID_ATTACHMENT");
    CHECK(result.failedItems[1].reason == "INVALID_ATTACHMENT");
    CHECK(env.Exists("bad-list"));
    CHECK(env.Exists("linked"));
    CHECK(fs::exists(image));
    CHECK(fs::exists(outside));
}

TEST_CASE("Alarm deletion retains attachments when database deletion fails", "[alarm-delete]") {
    Environment env;
    env.Insert("blocked", "1", R"(["_full.jpg"])");
    const auto image   = env.Event("2020/03/04", "blocked_full.jpg");
    const auto feature = env.Feature("2020/03/05", "blocked.feature");
    env.Write(image);
    env.Write(feature);
    env.db->exec(
        "CREATE TRIGGER reject_alarm_delete BEFORE DELETE ON t_commonEvent "
        "BEGIN SELECT RAISE(ABORT, 'injected failure'); END");
    const auto result = env.service->DeleteEvents({"blocked"});
    REQUIRE(result.failedItems.size() == 1);
    CHECK(result.failedItems[0].reason == "DATABASE_FAILED");
    CHECK(env.Exists("blocked"));
    CHECK(fs::exists(image));
    CHECK(fs::exists(feature));
    env.db->exec("DROP TRIGGER reject_alarm_delete");
    CHECK(env.service->DeleteEvents({"blocked"}).deletedIds == std::vector<std::string>{"blocked"});
}

TEST_CASE("Alarm deletion rejects linked date directories and leaves the record intact", "[alarm-delete]") {
    Environment env;
    env.Insert("linked-date", "1", R"(["_full.jpg"])");
    const auto image = env.Event("2020/01/01", "linked-date_full.jpg");
    env.Write(image);
    const auto outside = fs::path(env.root) / "outside";
    fs::create_directories(outside);
    const auto year = fs::path(cosmo::path::GetRecordRootPath(false)) / "2020";
    fs::create_directories(year);
    fs::create_directory_symlink(outside, year / "01");
    const auto result = env.service->DeleteEvents({"linked-date"});
    REQUIRE(result.failedItems.size() == 1);
    CHECK(result.failedItems[0].reason == "FILE_ACCESS_FAILED");
    CHECK(env.Exists("linked-date"));
    CHECK(fs::exists(image));
}

TEST_CASE("Alarm deletion preserves visual audits shared by another event", "[alarm-delete]") {
    Environment env;
    env.Insert("audit-a");
    env.Insert("audit-b");
    env.db->exec(
        "INSERT INTO t_visualAuditV1(request_id,owner,created_ms,request) "
        "VALUES('shared-audit','test',0,'{}'),('exclusive-audit','test',0,'{}')");
    env.db->exec(
        "INSERT INTO t_visualAuditAlarmV1(event_id,request_id) VALUES "
        "('audit-a','shared-audit'),('audit-a','exclusive-audit'),('audit-b','shared-audit')");
    const auto first = env.service->DeleteEvents({"audit-a"});
    CHECK(first.deletedIds == std::vector<std::string>{"audit-a"});
    CHECK(env.db->execAndGet("SELECT count(*) FROM t_visualAuditV1 WHERE request_id='exclusive-audit'")
              .getInt() == 0);
    CHECK(
        env.db->execAndGet("SELECT count(*) FROM t_visualAuditV1 WHERE request_id='shared-audit'").getInt() ==
        1);
    CHECK(env.db->execAndGet("SELECT count(*) FROM t_visualAuditAlarmV1 WHERE event_id='audit-b'").getInt() ==
          1);
    const auto second = env.service->DeleteEvents({"audit-b"});
    CHECK(second.deletedIds == std::vector<std::string>{"audit-b"});
    CHECK(
        env.db->execAndGet("SELECT count(*) FROM t_visualAuditV1 WHERE request_id='shared-audit'").getInt() ==
        0);
}

TEST_CASE("Alarm deletion rolls back the record and retries after feature unlink fails", "[alarm-delete]") {
    if (geteuid() == 0)
        SKIP("Requires a non-root user to exercise file permissions");
    Environment env;
    env.Insert("retry", "1", R"(["_full.jpg"])");
    const auto image   = env.Event("2020/01/01", "retry_full.jpg");
    const auto feature = env.Feature("2019/12/31", "retry.feature");
    env.Write(image);
    env.Write(feature);
    env.db->exec(
        "INSERT INTO t_visualAuditV1(request_id,owner,created_ms,request) "
        "VALUES('retry-audit','test',0,'{}')");
    env.db->exec("INSERT INTO t_visualAuditAlarmV1(event_id,request_id) VALUES('retry','retry-audit')");
    struct RestorePermissions {
        fs::path path;
        ~RestorePermissions() {
            std::error_code error;
            fs::permissions(path, fs::perms::owner_all, fs::perm_options::add, error);
        }
    } restore{feature.parent_path()};
    fs::permissions(feature.parent_path(), fs::perms::owner_read | fs::perms::owner_exec,
                    fs::perm_options::replace);
    const auto failed = env.service->DeleteEvents({"retry"});
    REQUIRE(failed.failedItems.size() == 1);
    CHECK(failed.failedItems[0].reason == "FILE_DELETE_FAILED");
    CHECK(env.Exists("retry"));
    CHECK(
        env.db->execAndGet("SELECT count(*) FROM t_visualAuditV1 WHERE request_id='retry-audit'").getInt() ==
        1);
    CHECK_FALSE(fs::exists(image));
    CHECK(fs::exists(feature));
    fs::permissions(feature.parent_path(), fs::perms::owner_all, fs::perm_options::replace);
    const auto retried = env.service->DeleteEvents({"retry"});
    CHECK(retried.deletedIds == std::vector<std::string>{"retry"});
    CHECK_FALSE(env.Exists("retry"));
    CHECK(
        env.db->execAndGet("SELECT count(*) FROM t_visualAuditV1 WHERE request_id='retry-audit'").getInt() ==
        0);
    CHECK_FALSE(fs::exists(feature));
}
