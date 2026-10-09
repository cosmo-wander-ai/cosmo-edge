#include "service/event/LayaReviewStore.h"

#include <SQLiteCpp/SQLiteCpp.h>

#include <algorithm>
#include <filesystem>

#include "util/PathUtil.h"
namespace cosmo {
LayaReviewStore::LayaReviewStore(std::string path) : path_(std::move(path)) {}
LayaReviewStore& LayaReviewStore::Instance() {
    static LayaReviewStore store((std::filesystem::path(path::GetDbPath()) / "laya-review-v1.db").string());
    return store;
}
void LayaReviewStore::Init() {
    if (initialized_)
        return;
    SQLite::Database db(path_, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.setBusyTimeout(20);
    db.exec("PRAGMA journal_mode=WAL");
    db.exec(
        "CREATE TABLE IF NOT EXISTS review (id TEXT PRIMARY KEY, created INTEGER NOT NULL, record TEXT NOT "
        "NULL, result TEXT, publication TEXT NOT NULL)");
    db.exec("CREATE INDEX IF NOT EXISTS review_created ON review(created)");
    // Requests from a previous engine process can never complete a new task epoch.
    db.exec(
        "UPDATE review SET result='{\"decision\":\"unknown\",\"reason\":\"engine_restarted\"}' WHERE result "
        "IS NULL");
    db.exec("UPDATE review SET publication='interrupted' WHERE publication='pending'");
    initialized_ = true;
}
bool LayaReviewStore::Begin(const nlohmann::json& r) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        Init();
        SQLite::Database db(path_, SQLite::OPEN_READWRITE);
        db.setBusyTimeout(20);
        SQLite::Transaction tx(db);
        SQLite::Statement q(db, "INSERT OR IGNORE INTO review VALUES(?,?,?,NULL,?)");
        q.bind(1, r.at("event_id").get<std::string>());
        q.bind(2, r.at("created_ms").get<int64_t>());
        q.bind(3, r.dump());
        q.bind(4, r.value("publication", std::string("pending")));
        if (q.exec() != 1)
            return false;
        // Bounded audit retention: newest 10000 candidates, independent of alarm media.
        db.exec(
            "DELETE FROM review WHERE id IN (SELECT id FROM review ORDER BY created DESC, rowid DESC LIMIT "
            "-1 OFFSET 10000)");
        tx.commit();
        return true;
    } catch (...) {
        return false;
    }
}
bool LayaReviewStore::Finish(const std::string& id, const nlohmann::json& result) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        Init();
        SQLite::Database db(path_, SQLite::OPEN_READWRITE);
        db.setBusyTimeout(20);
        SQLite::Statement q(db, "UPDATE review SET result=? WHERE id=? AND result IS NULL");
        q.bind(1, result.dump());
        q.bind(2, id);
        return q.exec() == 1;
    } catch (...) {
        return false;
    }
}
bool LayaReviewStore::Publication(const std::string& id, const std::string& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        Init();
        SQLite::Database db(path_, SQLite::OPEN_READWRITE);
        db.setBusyTimeout(20);
        SQLite::Statement q(db, "UPDATE review SET publication=? WHERE id=? AND publication='pending'");
        q.bind(1, status);
        q.bind(2, id);
        return q.exec() == 1;
    } catch (...) {
        return false;
    }
}
nlohmann::json LayaReviewStore::Page(const std::string& id, int page, int size) {
    std::lock_guard<std::mutex> lock(mutex_);
    Init();
    SQLite::Database db(path_, SQLite::OPEN_READONLY);
    db.setBusyTimeout(20);
    size = std::clamp(size, 1, 100);
    page = std::clamp(page, 1, 10000);
    SQLite::Statement count(db, "SELECT COUNT(*) FROM review WHERE (?='' OR id=?)");
    count.bind(1, id);
    count.bind(2, id);
    count.executeStep();
    nlohmann::json out = {{"total", count.getColumn(0).getInt64()},
                          {"rows", nlohmann::json::array()},
                          {"retention_limit", 10000}};
    SQLite::Statement q(db,
                        "SELECT record,result,publication FROM review WHERE (?='' OR id=?) ORDER BY created "
                        "DESC,rowid DESC LIMIT ? OFFSET ?");
    q.bind(1, id);
    q.bind(2, id);
    q.bind(3, size);
    q.bind(4, (page - 1) * size);
    while (q.executeStep()) {
        auto r           = nlohmann::json::parse(q.getColumn(0).getString());
        r["result"]      = q.getColumn(1).isNull() ? nlohmann::json(nullptr)
                                                   : nlohmann::json::parse(q.getColumn(1).getString());
        r["publication"] = q.getColumn(2).getString();
        out["rows"].push_back(std::move(r));
    }
    return out;
}
}  // namespace cosmo
