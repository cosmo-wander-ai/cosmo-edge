#include <SQLiteCpp/SQLiteCpp.h>
#include <sqlite3.h>

#include <atomic>
#include <filesystem>
#include <thread>

#include "catch_amalgamated.hpp"
#include "db/TaskEventDao.h"
#include "db/VisualAuditDao.h"
#include "util/UuidUtil.h"

namespace {
using Json = nlohmann::json;
using cosmo::db::TaskEventDao;
using cosmo::db::TaskEventData;
using cosmo::db::VisualAuditDao;

Json Request(const std::string& id, int count = 3) {
    Json request{{"protocol", "visual-decision/v1"},
                 {"profile", "laya_v"},
                 {"request_id", id},
                 {"task_id", "task"},
                 {"run_epoch", "epoch"},
                 {"config_revision", "revision"},
                 {"frame_id", "frame"},
                 {"roi_id", "roi-" + id},
                 {"manifest_sha256", std::string(64, 'a')},
                 {"items", Json::array()}};
    for (int i = 0; i < count; ++i)
        request["items"].push_back({{"item_id", "item-" + std::to_string(i)},
                                    {"question_id", "question-" + std::to_string(i)},
                                    {"question_version", 1},
                                    {"compiled_sha256", std::string(64, 'b')}});
    return request;
}

Json Result(const Json& request, int completed = 3) {
    auto result      = request;
    const int count  = result.at("items").size();
    result["status"] = completed == 0 ? "unknown" : completed == count ? "completed" : "partial";
    if (completed == 0)
        result["reason"] = "deadline_exceeded";
    for (int i = 0; i < count; ++i) {
        auto& item                 = result["items"][i];
        item["status"]             = i < completed ? "completed" : "unknown";
        item["business_qualified"] = false;
        if (i < completed) {
            item["answer"]      = i % 2 == 0;
            item["probability"] = .83;
        } else
            item["reason"] = "deadline_exceeded";
    }
    return result;
}

TaskEventData Event(const std::string& id) {
    TaskEventData event;
    event.id        = id;
    event.timestamp = 1000;
    event.category  = "1";
    event.property  = "{}";
    return event;
}

struct Fixture {
    SQLite::Database db{":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE | SQLite::OPEN_FULLMUTEX};
    TaskEventDao events{db};
    VisualAuditDao audits{db};
    Fixture() {
        events.CreateTable();
    }
    Json Row(const std::string& id) {
        return audits.Page("", id).at("rows").at(0);
    }
};
}  // namespace

TEST_CASE("Visual audit: pending batch retains each question identity and first completion",
          "[visual-audit]") {
    Fixture f;
    auto request           = Request("batch");
    request["prompt"]      = "must not be persisted";
    request["image_bytes"] = "must not be persisted";
    REQUIRE(f.audits.Begin(request, "engine-one", 100));
    auto row = f.Row("batch");
    REQUIRE(row["items"].size() == 3);
    REQUIRE(row["response"].is_null());
    REQUIRE_FALSE(row["request"].contains("prompt"));
    REQUIRE_FALSE(row["request"].contains("image_bytes"));
    REQUIRE_FALSE(f.audits.Begin(Request("batch", 1), "other-owner", 200));
    auto response = Result(Request("batch"), 2);
    REQUIRE(f.audits.Finish("batch", response, 110));
    REQUIRE_FALSE(f.audits.Finish("batch", Result(Request("batch")), 120));
    row = f.Row("batch");
    REQUIRE(row["response"] == response);
    REQUIRE(row["finished_ms"] == 110);
    for (size_t i = 0; i < 3; ++i) {
        REQUIRE(row["items"][i]["identity"] == Request("batch")["items"][i]);
        REQUIRE(row["items"][i]["result"] == response["items"][i]);
    }
    REQUIRE(f.audits.Delivery("batch", "returned"));
    REQUIRE_FALSE(f.audits.Delivery("batch", "publication_failed"));
    REQUIRE(f.Row("batch")["delivery"] == "returned");
}

TEST_CASE("Visual audit: cross ROI run and question mismatches cannot finish a pending batch",
          "[visual-audit]") {
    Fixture f;
    auto request = Request("strict");
    REQUIRE(f.audits.Begin(request, "owner", 100));
    for (const auto* field :
         {"request_id", "roi_id", "frame_id", "run_epoch", "config_revision", "task_id", "manifest_sha256"}) {
        auto wrong   = Result(request);
        wrong[field] = "wrong";
        REQUIRE_THROWS(f.audits.Finish("strict", wrong, 110));
        REQUIRE(f.Row("strict")["response"].is_null());
    }
    for (const auto* field : {"item_id", "question_id", "question_version", "compiled_sha256"}) {
        auto wrong               = Result(request);
        wrong["items"][1][field] = "wrong";
        REQUIRE_THROWS(f.audits.Finish("strict", wrong, 110));
    }
    auto wrong = Result(request);
    std::swap(wrong["items"][0], wrong["items"][2]);
    REQUIRE_THROWS(f.audits.Finish("strict", wrong, 110));
    wrong           = Result(request);
    wrong["status"] = "partial";
    REQUIRE_THROWS(f.audits.Finish("strict", wrong, 110));
    REQUIRE(f.audits.Finish("strict", Result(request), 120));
}

TEST_CASE("Visual audit: injected mid batch write failures leave no partial completion", "[visual-audit]") {
    Fixture f;
    REQUIRE(f.audits.Begin(Request("atomic"), "owner", 100));
    f.db.exec(
        "CREATE TRIGGER fail_second_item BEFORE UPDATE ON t_visualAuditItemV1 "
        "WHEN NEW.ordinal=1 BEGIN SELECT RAISE(ABORT,'injected_io_failure'); END;");
    REQUIRE_THROWS(f.audits.Finish("atomic", Result(Request("atomic")), 200));
    auto row = f.Row("atomic");
    REQUIRE(row["response"].is_null());
    for (const auto& item : row["items"])
        REQUIRE(item["result"].is_null());
    f.db.exec("DROP TRIGGER fail_second_item");
    REQUIRE(f.audits.Finish("atomic", Result(Request("atomic")), 210));
    f.db.exec(
        "CREATE TRIGGER fail_second_insert BEFORE INSERT ON t_visualAuditItemV1 "
        "WHEN NEW.ordinal=1 BEGIN SELECT RAISE(ABORT,'injected_io_failure'); END;");
    REQUIRE_THROWS(f.audits.Begin(Request("failed-begin"), "owner", 300));
    REQUIRE(f.audits.Page("", "failed-begin")["total"] == 0);
    REQUIRE(f.db.execAndGet("SELECT COUNT(*) FROM t_visualAuditItemV1 WHERE request_id='failed-begin'")
                .getInt() == 0);
}

TEST_CASE("Visual audit: alarm insertion links all contributors in one transaction", "[visual-audit]") {
    Fixture f;
    for (const auto* id : {"target-1", "target-2", "associated-flow"}) {
        REQUIRE(f.audits.Begin(Request(id), "owner", 100));
        REQUIRE(f.audits.Finish(id, Result(Request(id)), 120));
    }
    REQUIRE(f.events.Insert(Event("real-event"), {"target-1", "target-2", "associated-flow", "target-1"}));
    auto page = f.audits.Page("real-event");
    REQUIRE(page["total"] == 3);
    for (const auto& row : page["rows"]) {
        REQUIRE(row["events"] == Json::array({"real-event"}));
        REQUIRE(row["delivery"] == "alarm_linked");
    }
    REQUIRE_THROWS(f.events.Insert(Event("must-rollback"), {"target-1", "missing"}));
    cosmo::db::QueryTaskEventCondition condition;
    condition.id = "must-rollback";
    REQUIRE(f.events.Query(condition).total_count == 0);
    REQUIRE(f.audits.Page("must-rollback")["total"] == 0);
    REQUIRE(f.Row("target-1")["events"] == Json::array({"real-event"}));
    REQUIRE_THROWS(f.audits.LinkEvent("no-alarm", {"target-1"}));
}

TEST_CASE("Visual audit: nested audit transaction cannot commit the caller's outer transaction",
          "[visual-audit]") {
    Fixture f;
    REQUIRE(f.audits.Begin(Request("outer"), "owner", 100));
    f.events.Begin();
    REQUIRE(f.events.Insert(Event("kept-in-outer")));
    REQUIRE(f.events.Insert(Event("linked-in-outer"), {"outer"}));
    REQUIRE_THROWS(f.events.Insert(Event("failed-inner"), {"outer", "absent"}));
    cosmo::db::QueryTaskEventCondition all;
    REQUIRE(f.events.Query(all).total_count == 2);
    f.events.Rollback();
    REQUIRE(f.events.Query(all).total_count == 0);
    REQUIRE(f.Row("outer")["events"].empty());
    REQUIRE(f.Row("outer")["delivery"] == "pending");
}

TEST_CASE("Visual audit: real alarm cleanup handles shared decisions and late completion", "[visual-audit]") {
    Fixture f;
    f.db.exec("PRAGMA foreign_keys=OFF");
    REQUIRE(f.audits.Begin(Request("shared"), "owner", 100));
    REQUIRE(f.audits.Begin(Request("exclusive"), "owner", 100));
    REQUIRE(f.events.Insert(Event("event-a"), {"shared", "exclusive"}));
    REQUIRE(f.events.Insert(Event("event-b"), {"shared"}));
    f.events.RemoveItems({"event-a"});
    REQUIRE(f.audits.Page("", "exclusive")["total"] == 0);
    REQUIRE_FALSE(f.audits.Finish("exclusive", Result(Request("exclusive")), 200));
    REQUIRE(f.Row("shared")["events"] == Json::array({"event-b"}));
    REQUIRE(f.audits.Finish("shared", Result(Request("shared")), 200));
    // The trigger also covers legacy direct SQL deletion and survives FK OFF.
    f.db.exec("DELETE FROM t_commonEvent WHERE rec_id='event-b'");
    REQUIRE(f.audits.Page()["total"] == 0);
    REQUIRE(f.db.execAndGet("SELECT COUNT(*) FROM t_visualAuditItemV1").getInt() == 0);
    REQUIRE(f.db.execAndGet("SELECT COUNT(*) FROM t_visualAuditAlarmV1").getInt() == 0);
    REQUIRE_FALSE(f.audits.Finish("shared", Result(Request("shared")), 210));
}

TEST_CASE("Visual audit: alarm deletion rollback restores the entire audit graph", "[visual-audit]") {
    Fixture f;
    REQUIRE(f.audits.Begin(Request("retained"), "owner", 100));
    REQUIRE(f.audits.Finish("retained", Result(Request("retained")), 110));
    REQUIRE(f.events.Insert(Event("alarm"), {"retained"}));
    f.db.exec(
        "CREATE TRIGGER fail_alarm_delete AFTER DELETE ON t_visualAuditV1 "
        "BEGIN SELECT RAISE(ABORT,'injected_failure'); END;");
    REQUIRE_THROWS(f.events.RemoveItems({"alarm"}));
    REQUIRE(f.audits.Page("alarm")["total"] == 1);
    REQUIRE(f.Row("retained")["items"].size() == 3);
    cosmo::db::QueryTaskEventCondition condition;
    condition.id = "alarm";
    REQUIRE(f.events.Query(condition).total_count == 1);
}

TEST_CASE("Visual audit: explicit recovery preserves completed and currently owned work", "[visual-audit]") {
    Fixture f;
    REQUIRE(f.audits.Begin(Request("pending-old"), "old-owner", 100));
    REQUIRE(f.audits.Begin(Request("completed-old"), "old-owner", 100));
    REQUIRE(f.audits.Finish("completed-old", Result(Request("completed-old"), 2), 110));
    REQUIRE(f.events.Insert(Event("before-crash"), {"completed-old"}));
    REQUIRE(f.audits.Begin(Request("current"), "current-owner", 120));
    VisualAuditDao reopened(f.db);
    reopened.CreateTable();
    REQUIRE(f.Row("pending-old")["response"].is_null());
    REQUIRE(reopened.RecoverInterrupted("current-owner", 200) == 1);
    REQUIRE(f.Row("current")["response"].is_null());
    REQUIRE(f.Row("pending-old")["delivery"] == "interrupted");
    REQUIRE(f.Row("pending-old")["response"]["reason"] == "engine_restarted");
    const auto recovered = f.Row("pending-old");
    for (const auto& item : recovered["items"])
        REQUIRE(item["result"]["reason"] == "engine_restarted");
    REQUIRE_FALSE(f.audits.Finish("pending-old", Result(Request("pending-old")), 210));
    REQUIRE(f.Row("completed-old")["response"] == Result(Request("completed-old"), 2));
    REQUIRE(f.Row("completed-old")["delivery"] == "alarm_linked");
    REQUIRE(reopened.RecoverInterrupted("current-owner", 220) == 0);
}

TEST_CASE("Visual audit: standalone retention never evicts active or alarm linked records",
          "[visual-audit]") {
    Fixture f;
    for (int i = 0; i < 5; ++i) {
        const auto id = "picture-" + std::to_string(i);
        REQUIRE(f.audits.Begin(Request(id), "owner", 100 + i));
        REQUIRE(f.audits.Finish(id, Result(Request(id)), 110));
        REQUIRE(f.audits.Delivery(id, "returned"));
    }
    REQUIRE(f.audits.Begin(Request("alarm-linked"), "owner", 1));
    REQUIRE(f.audits.Finish("alarm-linked", Result(Request("alarm-linked")), 2));
    REQUIRE(f.events.Insert(Event("alarm"), {"alarm-linked"}));
    REQUIRE(f.audits.Begin(Request("in-flight"), "owner", 1));
    REQUIRE(f.audits.Begin(Request("awaiting-publication"), "owner", 1));
    REQUIRE(f.audits.Finish("awaiting-publication", Result(Request("awaiting-publication")), 2));
    REQUIRE(f.audits.PruneUnlinked(102, 1) == 4);
    REQUIRE(f.audits.Page("", "picture-4")["total"] == 1);
    REQUIRE(f.audits.PruneUnlinked(1000, 0) == 1);
    REQUIRE(f.audits.Page()["total"] == 3);
    REQUIRE(f.audits.Page("alarm")["total"] == 1);
    REQUIRE(f.Row("in-flight")["response"].is_null());
    REQUIRE(f.Row("awaiting-publication")["delivery"] == "pending");
}

TEST_CASE("Visual audit: recovery does not scan completed result history",
          "[visual-audit][visual-audit-maintenance-scan]") {
    Fixture f;
    {
        SQLite::Transaction transaction(f.db);
        SQLite::Statement insert(
            f.db,
            "INSERT INTO t_visualAuditV1(request_id,owner,created_ms,finished_ms,request,response,"
            "delivery) VALUES(?,'owner',100,110,?,?,'returned')");
        for (int i = 0; i < 2000; ++i) {
            const auto id      = "history-" + std::to_string(i);
            const auto request = Request(id, 1);
            insert.bind(1, id);
            insert.bind(2, request.dump());
            insert.bind(3, Result(request, 1).dump());
            insert.exec();
            insert.reset();
        }
        transaction.commit();
    }
    REQUIRE(f.audits.Begin(Request("closed"), "owner", 120));
    REQUIRE(f.audits.Delivery("closed", "returned"));
    REQUIRE(f.audits.Begin(Request("active"), "owner", 120));
    int fullScanSteps = 0;
    sqlite3_trace_v2(
        f.db.getHandle(), SQLITE_TRACE_PROFILE,
        [](unsigned, void* context, void* statement, void*) {
            *static_cast<int*>(context) += sqlite3_stmt_status(static_cast<sqlite3_stmt*>(statement),
                                                               SQLITE_STMTSTATUS_FULLSCAN_STEP, 0);
            return 0;
        },
        &fullScanSteps);
    const auto recovered = f.audits.RecoverClosed(200);
    sqlite3_trace_v2(f.db.getHandle(), 0, nullptr, nullptr);
    CHECK(recovered == 1);
    // Database work must depend on pending records, not large completed JSON
    // history. The old full scan held the device service mutex past 250 ms.
    CHECK(fullScanSteps < 64);
    CHECK(f.Row("closed")["response"]["reason"] == "audit_result_write_failed");
    CHECK(f.Row("active")["response"].is_null());
    CHECK(f.db.execAndGet("SELECT COUNT(*) FROM t_visualAuditV1").getInt() == 2002);
}

TEST_CASE("Visual audit: malformed input and read only failures cannot masquerade as success",
          "[visual-audit]") {
    Fixture f;
    auto request = Request("invalid");
    request["items"].push_back(request["items"][0]);
    REQUIRE_THROWS(f.audits.Begin(request, "owner", 10));
    REQUIRE_THROWS(f.audits.Begin(Request("too-many", 9), "owner", 10));
    REQUIRE_THROWS(f.audits.Begin(Request("invalid"), "", 10));
    request           = Request("nul");
    request["roi_id"] = std::string("a\0b", 3);
    REQUIRE_THROWS(f.audits.Begin(request, "owner", 10));
    REQUIRE(f.audits.Page()["total"] == 0);
    REQUIRE(f.audits.Begin(Request("pending"), "owner", 10));
    f.db.exec("PRAGMA query_only=ON");
    REQUIRE_THROWS(f.audits.Begin(Request("read-only"), "owner", 10));
    REQUIRE_THROWS(f.audits.Finish("pending", Result(Request("pending")), 20));
    REQUIRE_THROWS(f.events.Insert(Event("failed-alarm"), {"pending"}));
    REQUIRE(f.Row("pending")["response"].is_null());
    REQUIRE(f.audits.Page("", "read-only")["total"] == 0);
}

TEST_CASE("Visual audit: paged filters bind quoted IDs and preserve item order", "[visual-audit]") {
    Fixture f;
    const std::string id = "quote' OR 1=1 --";
    REQUIRE(f.audits.Begin(Request(id), "owner", 100));
    REQUIRE(f.audits.Begin(Request("normal"), "owner", 100));
    REQUIRE(f.events.Insert(Event(id), {id}));
    REQUIRE(f.audits.Page(id)["total"] == 1);
    REQUIRE(f.audits.Page("", id)["rows"][0]["request_id"] == id);
    REQUIRE(f.audits.Page("missing' OR 1=1 --")["total"] == 0);
    REQUIRE(f.audits.Page("", "", 2, 1)["rows"].size() == 1);
    REQUIRE(f.audits.Page("", "", 2147483647, 100)["rows"].empty());
    REQUIRE(f.Row(id)["items"][0]["identity"]["item_id"] == "item-0");
}

TEST_CASE("Visual audit: shared connection threads cannot join another request transaction",
          "[visual-audit][concurrency]") {
    Fixture f;
    constexpr int kThreads = 4;
    constexpr int kEach    = 25;
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            VisualAuditDao audits(f.db);
            TaskEventDao events(f.db);
            try {
                for (int i = 0; i < kEach; ++i) {
                    const auto id = std::to_string(t) + "-" + std::to_string(i);
                    if (!audits.Begin(Request(id), "owner", 100) ||
                        !audits.Finish(id, Result(Request(id)), 110))
                        ++failures;
                    if (i % 2) {
                        try {
                            events.Insert(Event(id), {id, "missing"});
                            ++failures;
                        } catch (const SQLite::Exception&) {
                        }
                    }
                    if (!events.Insert(Event(id), {id}))
                        ++failures;
                    events.RemoveItems({id});
                    if (audits.Finish(id, Result(Request(id)), 200))
                        ++failures;
                }
            } catch (...) {
                ++failures;
            }
        });
    }
    for (auto& thread : threads)
        thread.join();
    REQUIRE(failures == 0);
    REQUIRE(f.audits.Page()["total"] == 0);
    REQUIRE(f.db.execAndGet("SELECT COUNT(*) FROM t_visualAuditItemV1").getInt() == 0);
    REQUIRE(f.db.execAndGet("SELECT COUNT(*) FROM t_commonEvent").getInt() == 0);
}

TEST_CASE("Visual audit: reopening a file database preserves linkage and explicit crash recovery",
          "[visual-audit]") {
    const auto path =
        std::filesystem::temp_directory_path() / ("visual-audit-" + cosmo::util::GenerateUUID() + ".db");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } cleanup{path};
    {
        SQLite::Database db(path.string(),
                            SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE | SQLite::OPEN_FULLMUTEX);
        TaskEventDao events(db);
        events.CreateTable();
        VisualAuditDao audits(db);
        REQUIRE(audits.Begin(Request("pending"), "old-process", 100));
        REQUIRE(audits.Begin(Request("stored"), "old-process", 100));
        REQUIRE(audits.Finish("stored", Result(Request("stored")), 110));
        REQUIRE(events.Insert(Event("alarm-before-exit"), {"stored"}));
    }
    {
        SQLite::Database db(path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_FULLMUTEX);
        TaskEventDao events(db);
        events.CreateTable();
        VisualAuditDao audits(db);
        REQUIRE(audits.Page("", "pending")["rows"][0]["response"].is_null());
        REQUIRE(audits.RecoverInterrupted("new-process", 200) == 1);
        REQUIRE(audits.Page("alarm-before-exit")["rows"][0]["response"] == Result(Request("stored")));
        events.RemoveItems({"alarm-before-exit"});
        REQUIRE(audits.Page("", "stored")["total"] == 0);
        REQUIRE(audits.Page("", "pending")["rows"][0]["delivery"] == "interrupted");
    }
}

TEST_CASE("Visual audit: a busy commit releases the outer transaction before the reader finishes",
          "[visual-audit][busy-commit]") {
    const auto path =
        std::filesystem::temp_directory_path() / ("visual-busy-" + cosmo::util::GenerateUUID() + ".db");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } cleanup{path};
    SQLite::Database writer(path.string(),
                            SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE | SQLite::OPEN_FULLMUTEX);
    TaskEventDao(writer).CreateTable();
    writer.setBusyTimeout(20);
    SQLite::Database reader(path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_FULLMUTEX);
    reader.exec("BEGIN");
    REQUIRE(reader.execAndGet("SELECT COUNT(*) FROM t_commonEvent").getInt() == 0);
    VisualAuditDao audits(writer);
    REQUIRE_THROWS_AS(audits.Begin(Request("blocked"), "owner", 100), SQLite::Exception);
    // ROLLBACK TO followed by RELEASE can itself need the same exclusive lock.
    // The failed write must leave no transaction or pending lock behind.
    CHECK(sqlite3_get_autocommit(writer.getHandle()) == 1);
    SQLite::Database observer(path.string(), SQLite::OPEN_READONLY | SQLite::OPEN_FULLMUTEX);
    CHECK_NOTHROW(observer.execAndGet("SELECT COUNT(*) FROM t_commonEvent"));
    reader.exec("ROLLBACK");
    REQUIRE(audits.Begin(Request("next"), "owner", 200));
    REQUIRE(audits.Page()["total"] == 1);
    REQUIRE(sqlite3_get_autocommit(writer.getHandle()) == 1);
    REQUIRE(TaskEventDao(reader).Insert(Event("next-alarm"), {"next"}));
}
