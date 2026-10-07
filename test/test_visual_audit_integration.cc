#include <SQLiteCpp/SQLiteCpp.h>

#include <atomic>
#include <cmath>
#include <filesystem>
#include <future>

#include "api/MessageEventHandler.h"
#include "catch_amalgamated.hpp"
#include "db/TaskEventDao.h"
#include "db/VisualAuditDao.h"
#include "flow/alarm/AlarmVisualState.h"
#include "flow/common/VisualJudgment.h"
#include "mock/MockAlgorithmService.h"
#include "mock/MockDbService.h"
#include "mock/MockNetworkConfig.h"
#include "service/ai/impl/VisualAuditServiceImpl.h"
#include "service/ai/impl/VisualDecisionServiceImpl.h"
#include "service/ai/impl/VisualQuestionServiceImpl.h"
#include "service/event/impl/AlarmRecordServiceImpl.h"
#include "support/ScopedServiceOverride.h"
#include "util/UuidUtil.h"

namespace {
using namespace cosmo;
using namespace cosmo::service;
using namespace std::chrono_literals;
using Json = nlohmann::json;

struct AuditFile {
    std::string path =
        (std::filesystem::temp_directory_path() / ("visual-service-" + util::GenerateUUID() + ".db"))
            .string();
    std::shared_ptr<SQLite::Database> database = std::make_shared<SQLite::Database>(
        path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE | SQLite::OPEN_FULLMUTEX);
    AuditFile() {
        db::TaskEventDao(*database).CreateTable();
    }
    AuditFile(const AuditFile&)            = delete;
    AuditFile& operator=(const AuditFile&) = delete;
    ~AuditFile() {
        database.reset();
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};

VisualDecisionOptions Options(size_t capacity = 3) {
    VisualDecisionOptions options;
    options.release      = {std::string(64, 'a'),
                            {{"tower", std::string(64, 'b')},
                             {"adapter", std::string(64, 'c')},
                             {"decision", std::string(64, 'd')}}};
    options.capacity     = capacity;
    options.perTaskLimit = 1;
    return options;
}
VisualDecisionRequest Request(const std::string& roi = "roi-a") {
    VisualDecisionRequest request{"frame", roi, {}};
    for (int i = 0; i < 3; ++i)
        request.questions.push_back(
            {"item-" + std::to_string(i),
             "question-" + std::to_string(i),
             1,
             std::string(64, 'e'),
             2,
             {"false", "true"},
             {{"bucket", "image|noul:2"}, {"source", "temperature_image"}, {"value", 1.0}}});
    return request;
}
auto Run(const std::string& task = "task") {
    return std::make_shared<VisualDecisionRun>(task, "epoch", "revision");
}
VisualDecisionImage Image() {
    return {{1, 2, 3}, 32, 32};
}
Json Reply(const Json& request, const std::vector<uint8_t>& jpeg) {
    auto response = visual::Failure(request, "unused");
    response.erase("reason");
    response["status"]       = "completed";
    response["model_hashes"] = Options().release.modelHashes;
    response["image_sha256"] = visual::Sha256(jpeg.data(), jpeg.size());
    for (auto& item : response["items"]) {
        item.erase("reason");
        item["status"]             = "completed";
        item["ordered_options"]    = {"false", "true"};
        item["qtype"]              = 2;
        item["temperature"]        = Request().questions[0].temperature;
        item["probabilities"]      = {.1, .9};
        item["raw_option_logits"]  = {std::log(.1), std::log(.9)};
        item["raw_action_logits"]  = {0., 1.};
        item["top1"]               = "true";
        item["top2_margin"]        = .8;
        item["decision_timing_ms"] = {
            {"h2d_ms", 1.}, {"launch_sync_ms", 38.}, {"d2h_ms", 1.}, {"total_ms", 40.}};
    }
    return response;
}
const auto Transport = [](const auto&, const Json& request, const auto& jpeg, auto) {
    return Reply(request, jpeg);
};
Json Record(const VisualDecisionResult& result) {
    return {{"provider", "laya_v"},
            {"audit", result.audit.metadata},
            {"request", result.request},
            {"result", result.response}};
}
std::string Id(const VisualDecisionResult& result) {
    return result.request.at("request_id").get<std::string>();
}
Json Row(IVisualAuditService& service, const std::string& id) {
    return service.Page("", id, 1, 20).at("rows").at(0);
}
struct Gate {
    std::promise<void> entered, opened;
    std::shared_future<void> release{opened.get_future().share()};
    void Open() {
        try {
            opened.set_value();
        } catch (const std::future_error&) {
        }
    }
    ~Gate() {
        Open();
    }
};
}  // namespace

TEST_CASE("Visual audit integration: numerical service persists pending before ROI and immutable final items",
          "[visual-audit-integration]") {
    AuditFile file;
    VisualAuditServiceImpl audit(file.path);
    std::atomic<bool> pending{false};
    VisualDecisionServiceImpl service(
        Options(),
        [&](const auto&, const Json& request, const auto& jpeg, auto) {
            const auto row = Row(audit, request.at("request_id"));
            pending        = row["response"].is_null() && row["items"].size() == 3;
            return Reply(request, jpeg);
        },
        &audit);
    auto result = service.Decide(Request(), Run(), Image, 1s);
    REQUIRE(result.AllCompleted());
    REQUIRE(pending);
    REQUIRE(result.audit.metadata["begin"] == "stored");
    REQUIRE(result.audit.metadata["finish"] == "stored");
    REQUIRE(Row(audit, Id(result))["response"] == result.response);
    REQUIRE(Row(audit, Id(result))["request"]["roi_id"] == "roi-a");
    auto second = service.Decide(Request("roi-b"), Run(), Image, 1s);
    REQUIRE(second.AllCompleted());
    REQUIRE(Id(result) != Id(second));
    result.audit.lease->Seal("returned");
    REQUIRE(Row(audit, Id(result))["delivery"] == "returned");
}

TEST_CASE("Visual audit integration: concurrent alarm readers do not block durable decisions",
          "[visual-audit-integration][visual-audit-concurrent-reader]") {
    const bool readAtCompletion = GENERATE(false, true);
    AuditFile file;
    VisualAuditServiceImpl audit(file.path);
    SQLite::Database reader(file.path, SQLite::OPEN_READWRITE | SQLite::OPEN_FULLMUTEX);
    auto holdRead = [&] {
        reader.exec("BEGIN");
        reader.execAndGet("SELECT COUNT(*) FROM t_visualAuditV1");
    };
    VisualDecisionServiceImpl service(
        Options(),
        [&](const auto&, const Json& request, const auto& jpeg, auto) {
            if (readAtCompletion)
                holdRead();
            return Reply(request, jpeg);
        },
        &audit);
    if (!readAtCompletion)
        holdRead();
    const auto result = service.Decide(Request(), Run(), Image, 1s);
    reader.exec("ROLLBACK");
    CHECK(result.AllCompleted());
    CHECK(result.audit.metadata["begin"] == "stored");
    CHECK(result.audit.metadata["finish"] == "stored");
    CHECK(Row(audit, Id(result))["response"] == result.response);
    CHECK(audit.Status()["begin_failed"] == 0);
    CHECK(audit.Status()["finish_failed"] == 0);
    CHECK(reader.execAndGet("PRAGMA journal_mode").getString() == "wal");
}

TEST_CASE("Visual audit integration: transient database writers preserve admission and model results",
          "[visual-audit-integration][visual-audit-transient-write]") {
    const bool blockAtCompletion = GENERATE(false, true);
    AuditFile file;
    VisualAuditServiceImpl audit(file.path);
    std::future<void> writer;
    auto blockBriefly = [&] {
        std::promise<void> locked;
        auto ready = locked.get_future();
        writer     = std::async(std::launch::async, [&, locked = std::move(locked)]() mutable {
            SQLite::Database connection(file.path, SQLite::OPEN_READWRITE | SQLite::OPEN_FULLMUTEX);
            connection.exec("BEGIN EXCLUSIVE");
            locked.set_value();
            std::this_thread::sleep_for(80ms);
            connection.exec("COMMIT");
        });
        ready.get();
    };
    VisualDecisionServiceImpl service(
        Options(),
        [&](const auto&, const Json& request, const auto& jpeg, auto) {
            if (blockAtCompletion)
                blockBriefly();
            return Reply(request, jpeg);
        },
        &audit);
    if (!blockAtCompletion)
        blockBriefly();
    auto result = service.Decide(Request(), Run(), Image, 1s);
    writer.get();
    CHECK(result.AllCompleted());
    CHECK(result.audit.metadata["begin"] == "stored");
    CHECK(result.audit.metadata["finish"] == "stored");
    if (result.audit.Begun())
        CHECK(Row(audit, Id(result))["response"] == result.response);
    CHECK(audit.Status()["busy_retries"].get<uint64_t>() > 0);
}

TEST_CASE("Visual audit integration: admission uses the caller deadline during a durable writer",
          "[visual-audit-integration][visual-audit-admission-deadline]") {
    const bool shortDeadline = GENERATE(false, true);
    AuditFile file;
    VisualAuditServiceImpl audit(file.path);
    std::atomic<int> calls{0};
    std::promise<void> locked;
    auto ready  = locked.get_future();
    auto writer = std::async(std::launch::async, [&, locked = std::move(locked)]() mutable {
        SQLite::Database connection(file.path, SQLite::OPEN_READWRITE | SQLite::OPEN_FULLMUTEX);
        connection.exec("BEGIN IMMEDIATE");
        locked.set_value();
        // Device commits exceeded 500 ms while admission was limited to 250 ms.
        // Reproduce a writer collision without relying on the host's fsync speed.
        std::this_thread::sleep_for(400ms);
        connection.exec("COMMIT");
    });
    ready.get();
    VisualDecisionServiceImpl service(
        Options(),
        [&](const auto&, const Json& request, const auto& jpeg, auto) {
            ++calls;
            return Reply(request, jpeg);
        },
        &audit);
    const auto started = std::chrono::steady_clock::now();
    auto result        = service.Decide(Request(), Run(), Image, shortDeadline ? 50ms : 3s);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    writer.get();
    if (shortDeadline) {
        CHECK(elapsed < 200ms);
        CHECK(calls == 0);
        CHECK_FALSE(result.AllCompleted());
        CHECK(result.audit.metadata["begin"] == "busy");
    } else {
        CHECK(elapsed < 2s);
        CHECK(calls == 1);
        REQUIRE(result.AllCompleted());
        CHECK(result.audit.metadata["begin"] == "stored");
        CHECK(Row(audit, Id(result))["response"] == result.response);
        CHECK(audit.Status()["begin_failed"] == 0);
    }
}

TEST_CASE("Visual audit integration: completed decisions survive a longer concurrent writer",
          "[visual-audit-integration][visual-audit-result-write]") {
    AuditFile file;
    VisualAuditServiceImpl audit(file.path);
    std::future<void> writer;
    VisualDecisionServiceImpl service(
        Options(),
        [&](const auto&, const Json& request, const auto& jpeg, auto) {
            std::promise<void> locked;
            auto ready = locked.get_future();
            writer     = std::async(std::launch::async, [&, locked = std::move(locked)]() mutable {
                SQLite::Database connection(file.path, SQLite::OPEN_READWRITE | SQLite::OPEN_FULLMUTEX);
                connection.exec("BEGIN IMMEDIATE");
                locked.set_value();
                // The BM1688 mixed soak observed a completed model result lose
                // its durable record after a roughly 400 ms writer collision.
                std::this_thread::sleep_for(400ms);
                connection.exec("COMMIT");
            });
            ready.get();
            return Reply(request, jpeg);
        },
        &audit);
    const auto started = std::chrono::steady_clock::now();
    auto result        = service.Decide(Request(), Run(), Image, 3s);
    writer.get();
    CHECK(std::chrono::steady_clock::now() - started < 2s);
    REQUIRE(result.AllCompleted());
    CHECK(result.audit.metadata["finish"] == "stored");
    CHECK(audit.Status()["finish_failed"] == 0);
    CHECK(Row(audit, Id(result))["response"] == result.response);
    result.audit.lease->Seal("returned");
    audit.Maintain();
    CHECK(Row(audit, Id(result))["response"] == result.response);
    CHECK(audit.Status()["closed_results_recovered"] == 0);
}

TEST_CASE(
    "Visual audit integration: real alarm service atomically links associated ROI decisions and cleanup",
    "[visual-audit-integration]") {
    AuditFile file;
    test::MockDbService mockDb;
    ALLOW_CALL(mockDb, GetDb()).LR_RETURN(file.database);
    test::ScopedServiceOverride<IDbService> dbRegistration(mockDb);
    AlarmRecordServiceImpl alarms;
    VisualAuditServiceImpl audit(file.path);
    VisualDecisionServiceImpl service(Options(), Transport, &audit);
    auto first  = service.Decide(Request("roi-1"), Run("task-1"), Image, 1s);
    auto second = service.Decide(Request("roi-2"), Run("task-2"), Image, 1s);
    REQUIRE(first.AllCompleted());
    REQUIRE(second.AllCompleted());
    AlarmRecordUnit event;
    event.id        = "real-event";
    event.category  = "1";
    event.timestamp = 100;
    event.property  = Json{{"visualJudgments", {Record(first), Record(second)}}}.dump();
    REQUIRE(alarms.Insert(event));
    REQUIRE(audit.Page(event.id, "", 1, 20)["total"] == 2);
    REQUIRE(Row(audit, Id(first))["delivery"] == "alarm_linked");
    REQUIRE(Json::parse(event.property)["visualJudgments"][0]["audit"]["alarm_record"] == "stored");
    AlarmQueryCondition condition;
    condition.id = event.id;
    REQUIRE(alarms.QueryAlarmRecords(condition, 0).totalCount == 1);
    first.audit.lease.reset();
    second.audit.lease.reset();
    REQUIRE(Row(audit, Id(first))["delivery"] == "alarm_linked");
    alarms.RemoveItems({event.id});
    REQUIRE(audit.Page("", "", 1, 20)["total"] == 0);
}

TEST_CASE(
    "Visual audit integration: missing contributor prevents false alarm linkage and marks store failure",
    "[visual-audit-integration]") {
    AuditFile file;
    test::MockDbService mockDb;
    ALLOW_CALL(mockDb, GetDb()).LR_RETURN(file.database);
    test::ScopedServiceOverride<IDbService> dbRegistration(mockDb);
    AlarmRecordServiceImpl alarms;
    VisualAuditServiceImpl audit(file.path);
    VisualDecisionServiceImpl service(Options(), Transport, &audit);
    auto result                      = service.Decide(Request(), Run(), Image, 1s);
    auto missing                     = Record(result);
    missing["audit"]["request_id"]   = "absent";
    missing["request"]["request_id"] = "absent";
    AlarmRecordUnit event;
    event.id       = "must-not-exist";
    event.property = Json{{"visualJudgments", {Record(result), missing}}}.dump();
    REQUIRE_FALSE(alarms.Insert(event));
    REQUIRE(Json::parse(event.property)["visualJudgments"][0]["audit"]["alarm_record"] == "failed");
    AlarmQueryCondition condition;
    condition.id = event.id;
    REQUIRE(alarms.QueryAlarmRecords(condition, 0).totalCount == 0);
    REQUIRE(audit.Page(event.id, "", 1, 20)["total"] == 0);
}

TEST_CASE("Visual audit integration: capacity rejection is durable and does not prepare another ROI",
          "[visual-audit-integration]") {
    AuditFile file;
    VisualAuditServiceImpl audit(file.path);
    Gate gate;
    VisualDecisionServiceImpl service(
        Options(1),
        [&](const auto&, const Json& request, const auto& jpeg, auto) {
            gate.entered.set_value();
            gate.release.wait();
            return Reply(request, jpeg);
        },
        &audit);
    auto first =
        std::async(std::launch::async, [&] { return service.Decide(Request(), Run("one"), Image, 2s); });
    const bool entered = gate.entered.get_future().wait_for(1s) == std::future_status::ready;
    if (!entered)
        gate.Open();
    REQUIRE(entered);
    std::atomic<int> prepares{0};
    auto rejected = service.Decide(
        Request("rejected"), Run("two"),
        [&] {
            ++prepares;
            return Image();
        },
        100ms);
    gate.Open();
    REQUIRE(first.get().AllCompleted());
    REQUIRE(prepares == 0);
    REQUIRE(rejected.response["reason"] == "queue_full");
    REQUIRE(rejected.audit.metadata["finish"] == "stored");
    REQUIRE(Row(audit, Id(rejected))["response"] == rejected.response);
}

TEST_CASE(
    "Visual audit integration: deadline and cancellation outcomes cannot be replaced by late worker answers",
    "[visual-audit-integration]") {
    const bool cancel = GENERATE(false, true);
    AuditFile file;
    VisualAuditServiceImpl audit(file.path);
    Gate gate;
    VisualDecisionServiceImpl service(
        Options(),
        [&](const auto&, const Json& request, const auto& jpeg, auto) {
            gate.entered.set_value();
            gate.release.wait();
            return Reply(request, jpeg);
        },
        &audit);
    auto run           = Run();
    auto future        = std::async(std::launch::async,
                                    [&] { return service.Decide(Request(), run, Image, cancel ? 2s : 100ms); });
    const bool entered = gate.entered.get_future().wait_for(1s) == std::future_status::ready;
    if (!entered)
        gate.Open();
    REQUIRE(entered);
    if (cancel) {
        run->Invalidate();
        gate.Open();
    }
    auto result = future.get();
    gate.Open();
    service.Stop();
    REQUIRE(result.response["reason"] == (cancel ? "stale_task_run" : "deadline_exceeded"));
    REQUIRE(Row(audit, Id(result))["response"] == result.response);
    result.audit.lease->Seal("cancelled");
    REQUIRE(Row(audit, Id(result))["delivery"] == "cancelled");
}

TEST_CASE("Visual audit integration: a busy database blocks model admission with an explicit audit failure",
          "[visual-audit-integration]") {
    AuditFile file;
    VisualAuditServiceImpl audit(file.path);
    std::atomic<int> calls{0};
    VisualDecisionServiceImpl service(
        Options(),
        [&](const auto&, const auto& request, const auto& jpeg, auto) {
            ++calls;
            return Reply(request, jpeg);
        },
        &audit);
    file.database->exec("BEGIN EXCLUSIVE");
    const auto start   = std::chrono::steady_clock::now();
    auto result        = service.Decide(Request(), Run(), Image, 1s);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    file.database->exec("ROLLBACK");
    // The caller allows one second; one in-progress SQLite busy wait can add
    // at most its short timeout plus ordinary scheduler overhead.
    REQUIRE(elapsed < 1200ms);
    REQUIRE(calls == 0);
    REQUIRE(result.response["reason"] == "audit_store_unavailable");
    REQUIRE(result.audit.metadata["begin"] == "busy");
    REQUIRE(result.audit.metadata["finish"] == "not_attempted");
    REQUIRE(audit.Status()["begin_failed"] == 1);
    REQUIRE(audit.Page("", "", 1, 20)["total"] == 0);
}

TEST_CASE("Visual audit integration: finish write failure stays explicit and pending until recovery",
          "[visual-audit-integration]") {
    AuditFile file;
    std::string id;
    {
        VisualAuditServiceImpl audit(file.path);
        VisualDecisionServiceImpl service(
            Options(),
            [&](const auto&, const auto& request, const auto& jpeg, auto) {
                file.database->exec("BEGIN EXCLUSIVE");
                return Reply(request, jpeg);
            },
            &audit);
        const auto started = std::chrono::steady_clock::now();
        auto result        = service.Decide(Request(), Run(), Image, 1s);
        const auto elapsed = std::chrono::steady_clock::now() - started;
        file.database->exec("ROLLBACK");
        CHECK(elapsed < 1500ms);
        id = Id(result);
        REQUIRE(result.AllCompleted());
        REQUIRE(result.audit.metadata["begin"] == "stored");
        REQUIRE(result.audit.metadata["finish"] == "busy");
        REQUIRE(audit.Status()["finish_failed"] == 1);
        REQUIRE(Row(audit, id)["response"].is_null());
    }
    VisualAuditServiceImpl reopened(file.path);
    REQUIRE(Row(reopened, id)["response"]["reason"] == "engine_restarted");
    REQUIRE(reopened.Status()["recovered"] == 1);
}

TEST_CASE("Visual audit integration: preflight configuration rejection is stored without model inference",
          "[visual-audit-integration]") {
    AuditFile file;
    VisualAuditServiceImpl audit(file.path);
    test::ScopedServiceOverride<IVisualAuditService> registration(audit);
    VisualJudgment judgment("task", "helmet", false, {{"visual.mode", "filter"}}, {});
    std::atomic<int> prepares{0};
    auto result = judgment.Decide("frame", "roi", "", [&] {
        ++prepares;
        return Image();
    });
    REQUIRE(result.response["reason"] == "unqualified_filtering_disabled");
    REQUIRE(result.audit.metadata["finish"] == "stored");
    REQUIRE(Row(audit, Id(result))["response"] == result.response);
    REQUIRE(prepares == 0);
}

TEST_CASE("Visual audit integration: startup lock recovery retries before admitting model work",
          "[visual-audit-integration]") {
    AuditFile file;
    file.database->exec("BEGIN EXCLUSIVE");
    VisualAuditServiceImpl audit(file.path);
    file.database->exec("ROLLBACK");
    REQUIRE(audit.Status()["available"] == false);
    VisualDecisionServiceImpl service(Options(), Transport, &audit);
    REQUIRE_FALSE(service.Available());
    auto unavailable = service.Decide(Request(), Run(), Image, 1s);
    REQUIRE(unavailable.response["reason"] == "audit_store_unavailable");
    audit.Maintain();
    REQUIRE(audit.Status()["available"] == true);
    REQUIRE(audit.Status()["initialization_attempts"] == 2);
    REQUIRE(service.Available());
    REQUIRE(service.Decide(Request(), Run(), Image, 1s).AllCompleted());
}

TEST_CASE(
    "Visual audit integration: ended write failures become explicit unknowns without touching active "
    "requests",
    "[visual-audit-integration]") {
    AuditFile file;
    VisualAuditServiceImpl audit(file.path);
    VisualDecisionServiceImpl service(Options(), Transport, &audit);
    auto templateResult         = service.Decide(Request(), Run(), Image, 1s);
    auto closedRequest          = templateResult.request;
    closedRequest["request_id"] = "closed-write-failure";
    auto activeRequest          = closedRequest;
    activeRequest["request_id"] = "still-running";
    auto closed                 = audit.Begin(closedRequest);
    auto active                 = audit.Begin(activeRequest);
    REQUIRE(closed.Begun());
    REQUIRE(active.Begun());
    closed.lease->Seal("no_alarm");
    audit.Maintain();
    REQUIRE(Row(audit, "closed-write-failure")["response"]["reason"] == "audit_result_write_failed");
    REQUIRE(Row(audit, "still-running")["response"].is_null());
    REQUIRE(audit.Status()["closed_results_recovered"] == 1);
}

TEST_CASE("Visual audit integration: associated leases close only after the last unreported owner",
          "[visual-audit-integration]") {
    AuditFile file;
    VisualAuditServiceImpl audit(file.path);
    VisualDecisionServiceImpl service(Options(), Transport, &audit);
    auto result = service.Decide(Request(), Run(), Image, 1s);
    DataAlarmUnit source, associated;
    source.visualAuditLeases.push_back(result.audit.lease);
    alarm::AppendVisualState(associated, source);
    result.audit.lease.reset();
    source.visualAuditLeases.clear();
    REQUIRE(Row(audit, Id(result))["delivery"] == "pending");
    associated.visualAuditLeases.clear();
    REQUIRE(Row(audit, Id(result))["delivery"] == "no_alarm");
}

TEST_CASE("Visual audit integration: last owner delivery retries after a database lock clears",
          "[visual-audit-integration]") {
    AuditFile file;
    VisualAuditServiceImpl audit(file.path);
    VisualDecisionServiceImpl service(Options(), Transport, &audit);
    auto result = service.Decide(Request(), Run(), Image, 1s);
    file.database->exec("BEGIN EXCLUSIVE");
    result.audit.lease.reset();
    file.database->exec("ROLLBACK");
    REQUIRE(Row(audit, Id(result))["delivery"] == "pending");
    REQUIRE(audit.Status()["delivery_failed"] == 1);
    REQUIRE(audit.Status()["delivery_retry_pending"] == 1);
    audit.Maintain();
    REQUIRE(Row(audit, Id(result))["delivery"] == "no_alarm");
    REQUIRE(audit.Status()["delivery_retry_pending"] == 0);
    REQUIRE(audit.Status()["delivery_retry_dropped"] == 0);
}

TEST_CASE("Visual audit integration: typed query selects the actual audit store and reports errors",
          "[visual-audit-integration]") {
    AuditFile file;
    test::MockDbService mockDb;
    ALLOW_CALL(mockDb, GetDb()).LR_RETURN(file.database);
    test::ScopedServiceOverride<IDbService> dbRegistration(mockDb);
    AlarmRecordServiceImpl alarms;
    VisualAuditServiceImpl audit(file.path);
    VisualDecisionServiceImpl service(Options(), Transport, &audit);
    test::ScopedServiceOverride<IVisualAuditService> registration(audit);
    test::ScopedServiceOverride<IVisualDecisionService> decisions(service);
    test::MockAlgorithmService algorithms;
    test::MockNetworkConfig network;
    MessageEventHandler handler(alarms, algorithms, network);
    VisualQuestionServiceImpl compiler(VisualQuestionCompilerOptions{});
    test::ScopedServiceOverride<IVisualQuestionService> questions(compiler);
    auto result = service.Decide(Request(), Run(), Image, 1s);
    Event::MsgLayaReviewPageRecv query;
    query.format    = "typed-v1";
    query.requestId = Id(result);
    Json encoded    = query;
    query           = encoded.get<Event::MsgLayaReviewPageRecv>();
    std::error_condition error;
    auto response = handler.Handle(std::move(query), error);
    REQUIRE_FALSE(error);
    REQUIRE(response.resData["format"] == "typed-v1");
    REQUIRE(response.resData["total"] == 1);
    REQUIRE(response.resData["rows"][0]["response"] == result.response);
    REQUIRE(response.resData["audit_runtime"]["available"] == true);
    REQUIRE(response.resData["automatic_filtering"] == false);
    REQUIRE(response.resData["question_runtime"]["available"] == false);
    REQUIRE(response.resData["question_runtime"]["outstanding"] == 0);
    Event::MsgLayaReviewPageRecv invalid;
    invalid.format = "unknown-version";
    (void)handler.Handle(std::move(invalid), error);
    REQUIRE(error);
}

TEST_CASE("Visual audit preserves filtered decisions without an alarm row",
          "[visual-audit-integration][visual-policy]") {
    AuditFile file;
    VisualAuditServiceImpl audit(file.path);
    auto options                  = Options();
    auto request                  = Request();
    request.mode                  = "filter";
    request.policyId              = "fixture";
    request.qualificationRevision = std::string(64, 'f');
    options.acceptance            = {{"manifest_sha256", options.release.manifestSha256},
                                     {"profiles",
                                      {{{"id", "fixture"},
                                        {"status", "accepted"},
                                        {"task_ids", {"task"}},
                                        {"configuration_sha256", request.qualificationRevision},
                                        {"evidence_ref", "native-fixture"},
                                        {"aggregation", "all"},
                                        {"unknown", "keep"},
                                        {"questions",
                                         {{{"compiled_sha256", std::string(64, 'e')},
                                           {"positive_options", {"false"}},
                                           {"min_probability", .8},
                                           {"min_margin", .2}}}}}}}};
    VisualDecisionServiceImpl service(options, Transport, &audit);
    auto result = service.Decide(request, Run(), Image, 1s);
    REQUIRE(result.AllCompleted());
    REQUIRE_FALSE(result.Retain());
    result.audit.lease->Seal("filtered");
    const auto row = Row(audit, Id(result));
    CHECK(row["response"]["decision"]["verdict"] == "reject");
    CHECK(row["delivery"] == "filtered");
    CHECK(row["events"].empty());
    CHECK(file.database->execAndGet("SELECT COUNT(*) FROM t_commonEvent").getInt() == 0);
}
