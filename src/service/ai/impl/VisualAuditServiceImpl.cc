#include "service/ai/impl/VisualAuditServiceImpl.h"

#include <SQLiteCpp/SQLiteCpp.h>
#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <stdexcept>

#include "db/VisualAuditDao.h"
#include "util/Log.h"
#include "util/UuidUtil.h"

namespace cosmo::service {
namespace {
    using Json  = nlohmann::json;
    using Clock = std::chrono::steady_clock;
    using namespace std::chrono_literals;
    int64_t Now() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }
}  // namespace

struct VisualAuditServiceImpl::State {
    explicit State(std::string path) : databasePath(std::move(path)) {}
    const std::string databasePath;
    std::unique_ptr<SQLite::Database> database;
    std::timed_mutex mutex;
    const std::string owner{util::GenerateUUID()};
    std::atomic<bool> ready{false};
    std::atomic<size_t> recovered{0}, closedRecovered{0}, initializationAttempts{0};
    std::atomic<uint64_t> beginFailed{0}, finishFailed{0}, deliveryFailed{0}, maintenanceFailed{0};
    std::atomic<uint64_t> busyRetries{0};
    std::atomic<const char*> activeOperation{"idle"};
    std::mutex retryMutex;
    std::map<std::string, std::string> deliveryRetries;
    std::atomic<uint64_t> retryDropped{0};

    bool Initialize() {
        std::unique_lock<std::timed_mutex> lock(mutex, std::defer_lock);
        if (!lock.try_lock_for(20ms))
            return false;
        if (ready)
            return true;
        ++initializationAttempts;
        try {
            auto connection = std::make_unique<SQLite::Database>(
                databasePath, SQLite::OPEN_READWRITE | SQLite::OPEN_FULLMUTEX);
            connection->setBusyTimeout(20);
            if (!connection->tableExists("t_commonEvent") || !connection->tableExists("t_visualAuditV1"))
                return false;
            // Alarm queries use another connection. Rollback journaling lets a
            // reader block audit commits, dropping otherwise completed model
            // results under ordinary concurrent load. Persist WAL before
            // accepting work; keep fully synchronous durable writes.
            if (connection->execAndGet("PRAGMA journal_mode=WAL").getString() != "wal")
                return false;
            connection->exec("PRAGMA synchronous=FULL");
            sqlite3_trace_v2(
                connection->getHandle(), SQLITE_TRACE_PROFILE,
                [](unsigned, void*, void* statement, void* elapsed) {
                    const auto milliseconds = *static_cast<sqlite3_uint64*>(elapsed) / 1000000;
                    if (milliseconds >= 100) {
                        // sqlite3_sql keeps parameters unexpanded; never log
                        // request payloads, prompts or result values.
                        const char* sql = sqlite3_sql(static_cast<sqlite3_stmt*>(statement));
                        LOG_WARN("Visual audit SQL {} ms: {}", milliseconds,
                                 sql ? std::string(sql).substr(0, 80) : "unknown");
                    }
                    return 0;
                },
                nullptr);
            db::VisualAuditDao dao(*connection);
            recovered = dao.RecoverInterrupted(owner, Now());
            database  = std::move(connection);
            ready     = true;
            return true;
        } catch (...) {
            return false;
        }
    }

    std::string Execute(const char* name, const std::function<bool(db::VisualAuditDao&)>& operation,
                        std::chrono::milliseconds retryBudget = 250ms) {
        if (!ready)
            return "unavailable";
        const auto deadline = Clock::now() + retryBudget;
        for (;;) {
            if (Clock::now() >= deadline)
                return "busy";
            std::unique_lock<std::timed_mutex> lock(mutex, std::defer_lock);
            if (!lock.try_lock_until(deadline)) {
                LOG_WARN("Visual audit {} mutex wait expired; holder={}", name, activeOperation.load());
                return "busy";
            }
            activeOperation             = name;
            const auto operationStarted = Clock::now();
            try {
                db::VisualAuditDao dao(*database);
                const bool applied = operation(dao);
                activeOperation    = "idle";
                const auto elapsed =
                    std::chrono::duration<double, std::milli>(Clock::now() - operationStarted).count();
                if (elapsed >= 100)
                    LOG_WARN("Visual audit {} held connection for {} ms", name, elapsed);
                return applied ? "stored" : "not_applied";
            } catch (const SQLite::Exception& error) {
                activeOperation = "idle";
                if (error.getErrorCode() != SQLITE_BUSY && error.getErrorCode() != SQLITE_LOCKED)
                    return "write_failed";
            } catch (...) {
                activeOperation = "idle";
                return "invalid_record";
            }
            // Another engine connection can briefly own the writer lock. A
            // read-to-write upgrade may return BUSY immediately, so retry the
            // rolled-back operation, not only SQLite's individual statement.
            lock.unlock();
            if (Clock::now() >= deadline)
                return "busy";
            ++busyRetries;
            std::this_thread::sleep_until(std::min(deadline, Clock::now() + 5ms));
        }
    }

    void Delivery(const std::string& id, const std::string& status) {
        const auto result = Execute("delivery", [&](auto& dao) { return dao.Delivery(id, status); });
        // Not applied includes already-linked/deleted/terminal requests.
        // Retain bounded retries when last-owner delivery races a DB lock;
        // otherwise an ended request could remain pending until a restart.
        if (result != "stored" && result != "not_applied") {
            ++deliveryFailed;
            std::lock_guard<std::mutex> lock(retryMutex);
            if (deliveryRetries.size() < 4096 || deliveryRetries.count(id))
                deliveryRetries.emplace(id, status);
            else
                ++retryDropped;
        }
    }

    void RetryDeliveries() {
        std::map<std::string, std::string> pending;
        {
            std::lock_guard<std::mutex> lock(retryMutex);
            while (!deliveryRetries.empty() && pending.size() < 16)
                pending.insert(deliveryRetries.extract(deliveryRetries.begin()));
        }
        for (const auto& [id, status] : pending)
            Delivery(id, status);
    }
};

VisualAuditServiceImpl::VisualAuditServiceImpl(const std::string& databasePath)
    : state_(std::make_shared<State>(databasePath)) {
    // Recovery always precedes admission. A temporary startup lock is retried
    // by maintenance; no new work can enter before recovery succeeds.
    state_->Initialize();
    maintenance_ = std::thread([this] {
        std::unique_lock<std::mutex> lock(maintenanceMutex_);
        while (!maintenanceCv_.wait_for(lock, 60s, [&] { return stopped_; })) {
            lock.unlock();
            Maintain();
            lock.lock();
        }
    });
}

VisualAuditServiceImpl::~VisualAuditServiceImpl() {
    {
        std::lock_guard<std::mutex> lock(maintenanceMutex_);
        stopped_ = true;
    }
    maintenanceCv_.notify_all();
    if (maintenance_.joinable())
        maintenance_.join();
}

VisualAuditReceipt VisualAuditServiceImpl::Begin(const Json& request,
                                                 std::chrono::milliseconds waitBudget) noexcept {
    VisualAuditReceipt receipt;
    try {
        const auto id                  = request.at("request_id").get<std::string>();
        receipt.metadata["request_id"] = id;
        const auto started             = Clock::now();
        const auto budget              = std::clamp(waitBudget, 0ms, 1000ms);
        const auto result              = state_->Execute(
            "begin", [&](auto& dao) { return dao.Begin(request, state_->owner, Now()); }, budget);
        receipt.metadata["begin_retry_budget_ms"] = budget.count();
        receipt.metadata["begin"]                 = result;
        receipt.metadata["begin_ms"] =
            std::chrono::duration<double, std::milli>(Clock::now() - started).count();
        if (result == "stored") {
            // Leases may outlive registry teardown. Own only the independent
            // database state, never a raw service or registry pointer.
            receipt.lease = std::make_shared<VisualAuditLease>(
                [state = state_, id](const auto& status) { state->Delivery(id, status); });
        } else
            ++state_->beginFailed;
    } catch (...) {
        receipt.metadata["begin"] = "invalid_record";
        ++state_->beginFailed;
    }
    return receipt;
}

bool VisualAuditServiceImpl::Complete(VisualAuditReceipt& receipt, const Json& response) noexcept {
    if (!receipt.Begun())
        return false;
    try {
        const auto id      = receipt.metadata.at("request_id").get<std::string>();
        const auto started = Clock::now();
        // A model result is already available here. Allow an ordinary alarm
        // writer to commit before discarding that result's durable record.
        // Admission and other operations keep their short 250 ms budget;
        // persistent contention still returns an explicit failure.
        const auto result =
            state_->Execute("finish", [&](auto& dao) { return dao.Finish(id, response, Now()); }, 1000ms);
        receipt.metadata["finish"] = result;
        receipt.metadata["finish_ms"] =
            std::chrono::duration<double, std::milli>(Clock::now() - started).count();
        if (result == "stored")
            return true;
    } catch (...) {
        receipt.metadata["finish"] = "invalid_record";
    }
    ++state_->finishFailed;
    return false;
}

Json VisualAuditServiceImpl::Page(const std::string& eventId, const std::string& requestId, int page,
                                  int size) {
    if (!state_->ready)
        throw std::runtime_error("visual_audit_unavailable");
    // WAL readers can inspect the last committed snapshot while a durable
    // result is being flushed. Sharing the writer mutex instead made ordinary
    // review queries fail whenever fsync exceeded its 250 ms wait budget.
    SQLite::Database reader(state_->databasePath, SQLite::OPEN_READONLY | SQLite::OPEN_FULLMUTEX);
    reader.setBusyTimeout(250);
    return db::VisualAuditDao(reader).Page(eventId, requestId, page, size);
}

Json VisualAuditServiceImpl::Status() const {
    size_t retryCount;
    {
        std::lock_guard<std::mutex> lock(state_->retryMutex);
        retryCount = state_->deliveryRetries.size();
    }
    return {{"schema", 1},
            {"available", state_->ready.load()},
            {"recovered", state_->recovered.load()},
            {"closed_results_recovered", state_->closedRecovered.load()},
            {"initialization_attempts", state_->initializationAttempts.load()},
            {"begin_failed", state_->beginFailed.load()},
            {"finish_failed", state_->finishFailed.load()},
            {"delivery_failed", state_->deliveryFailed.load()},
            {"delivery_retry_pending", retryCount},
            {"delivery_retry_dropped", state_->retryDropped.load()},
            {"maintenance_failed", state_->maintenanceFailed.load()},
            {"busy_retries", state_->busyRetries.load()},
            {"lock_wait_ms", 250},
            {"operation_retry_budget_ms", 250},
            {"begin_retry_budget_max_ms", 1000},
            {"finish_retry_budget_ms", 1000},
            {"sqlite_busy_ms", 20},
            {"unlinked_limit", 10000},
            {"unlinked_age_hours", 24}};
}

void VisualAuditServiceImpl::Maintain() {
    if (!state_->ready && !state_->Initialize()) {
        ++state_->maintenanceFailed;
        return;
    }
    state_->RetryDeliveries();
    const auto status = state_->Execute("maintenance", [&](auto& dao) {
        state_->closedRecovered += dao.RecoverClosed(Now());
        dao.PruneUnlinked(std::max<int64_t>(0, Now() - 86400000), 10000);
        return true;
    });
    if (status != "stored")
        ++state_->maintenanceFailed;
}

}  // namespace cosmo::service
