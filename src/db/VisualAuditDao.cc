#include "db/VisualAuditDao.h"

#include <SQLiteCpp/SQLiteCpp.h>

#include <algorithm>
#include <set>
#include <stdexcept>

#include "db/ScopedDbSavepoint.h"

namespace cosmo::db {
namespace {
    using Json                            = nlohmann::json;
    constexpr const char* kIdentity[]     = {"request_id", "task_id", "run_epoch",      "config_revision",
                                             "frame_id",   "roi_id",  "manifest_sha256"};
    constexpr const char* kItemIdentity[] = {"item_id", "question_id", "question_version", "compiled_sha256"};

    void Require(bool valid) {
        if (!valid)
            throw std::invalid_argument("invalid_visual_audit_record");
    }

    bool ValidId(const std::string& id) {
        return !id.empty() && id.size() <= 256 && id.find('\0') == std::string::npos;
    }

    Json RequestIdentity(const Json& request) {
        Require(request.is_object() && request.dump().size() <= 65536);
        Json result = Json::object();
        for (const auto* key : kIdentity) {
            const auto& value = request.at(key);
            Require((std::string(key) == "manifest_sha256" && value.is_null()) ||
                    (value.is_string() && ValidId(value.get<std::string>())));
            result[key] = value;
        }
        for (const auto* key : {"protocol", "profile"}) {
            if (request.contains(key)) {
                Require(request.at(key).is_string() && ValidId(request.at(key).get<std::string>()));
                result[key] = request.at(key);
            }
        }
        const auto& items = request.at("items");
        Require(items.is_array() && items.size() <= 8);
        result["items"] = Json::array();
        std::set<std::string> seen;
        for (const auto& item : items) {
            Require(item.is_object());
            Json identity = Json::object();
            for (const auto* key : kItemIdentity)
                identity[key] = item.at(key);
            Require(identity.at("item_id").is_string() &&
                    ValidId(identity.at("item_id").get<std::string>()) &&
                    seen.insert(identity.at("item_id").get<std::string>()).second &&
                    identity.at("question_id").is_string() &&
                    ValidId(identity.at("question_id").get<std::string>()) &&
                    identity.at("question_version").is_number_integer() &&
                    identity.at("question_version").get<int64_t>() > 0 &&
                    (identity.at("compiled_sha256").is_null() ||
                     (identity.at("compiled_sha256").is_string() &&
                      ValidId(identity.at("compiled_sha256").get<std::string>()))));
            result["items"].push_back(std::move(identity));
        }
        // Deliberately omit image bytes, prompts and arbitrary caller metadata.
        return result;
    }

    void ValidateResponse(const Json& request, const Json& response) {
        Require(response.is_object() && response.dump().size() <= 262144);
        for (const auto* key : kIdentity)
            Require(response.at(key) == request.at(key));
        for (const auto* key : {"protocol", "profile"})
            if (request.contains(key))
                Require(response.at(key) == request.at(key));
        const auto& items = response.at("items");
        Require(items.is_array() && items.size() == request.at("items").size());
        size_t completed = 0;
        for (size_t i = 0; i < items.size(); ++i) {
            for (const auto* key : kItemIdentity)
                Require(items[i].at(key) == request.at("items")[i].at(key));
            const auto status = items[i].at("status").get<std::string>();
            Require(status == "completed" || status == "unknown");
            if (status == "completed")
                ++completed;
            else
                Require(items[i].at("reason").is_string() &&
                        !items[i].at("reason").get<std::string>().empty());
        }
        const std::string status = completed == 0              ? "unknown"
                                   : completed == items.size() ? "completed"
                                                               : "partial";
        Require(response.at("status") == status);
    }

    Json Interrupted(const Json& request, const std::string& reason = "engine_restarted") {
        auto result      = request;
        result["status"] = "unknown";
        result["reason"] = reason;
        for (auto& item : result["items"]) {
            item["status"]             = "unknown";
            item["reason"]             = reason;
            item["business_qualified"] = false;
        }
        return result;
    }
}  // namespace

void VisualAuditDao::CreateTable() {
    ScopedDbSavepoint transaction(Db());
    Db().exec(
        "CREATE TABLE IF NOT EXISTS t_visualAuditV1 ("
        "request_id TEXT PRIMARY KEY, owner TEXT NOT NULL, created_ms INTEGER NOT NULL,"
        "finished_ms INTEGER, request TEXT NOT NULL, response TEXT, delivery TEXT NOT NULL DEFAULT "
        "'pending');"
        "CREATE INDEX IF NOT EXISTS visual_audit_created_v1 ON t_visualAuditV1(created_ms, request_id);"
        // Recovery runs under the service connection mutex. Do not scan wide
        // completed JSON rows just to discover that no result needs recovery.
        "CREATE INDEX IF NOT EXISTS visual_audit_pending_result_v1 ON t_visualAuditV1(request_id) "
        "WHERE response IS NULL;"
        "CREATE INDEX IF NOT EXISTS visual_audit_retention_v1 ON t_visualAuditV1(created_ms,request_id) "
        "WHERE response IS NOT NULL AND delivery<>'pending';"
        "CREATE TABLE IF NOT EXISTS t_visualAuditItemV1 ("
        "request_id TEXT NOT NULL, ordinal INTEGER NOT NULL, item_id TEXT NOT NULL,"
        "identity TEXT NOT NULL, result TEXT, PRIMARY KEY(request_id, item_id), UNIQUE(request_id, ordinal));"
        "CREATE TABLE IF NOT EXISTS t_visualAuditAlarmV1 ("
        "event_id TEXT NOT NULL, request_id TEXT NOT NULL, PRIMARY KEY(event_id, request_id));"
        "CREATE INDEX IF NOT EXISTS visual_audit_link_request_v1 ON t_visualAuditAlarmV1(request_id);"
        // Explicit triggers work even when the legacy connection has foreign_keys OFF.
        "CREATE TRIGGER IF NOT EXISTS visual_audit_link_guard_v1 BEFORE INSERT ON t_visualAuditAlarmV1 BEGIN "
        "SELECT CASE WHEN NOT EXISTS(SELECT 1 FROM t_commonEvent WHERE rec_id=NEW.event_id) "
        "OR NOT EXISTS(SELECT 1 FROM t_visualAuditV1 WHERE request_id=NEW.request_id) "
        "THEN RAISE(ABORT,'visual_audit_link_missing') END; END;"
        "CREATE TRIGGER IF NOT EXISTS visual_audit_link_delivery_v1 AFTER INSERT ON t_visualAuditAlarmV1 "
        "BEGIN "
        "UPDATE t_visualAuditV1 SET delivery='alarm_linked' WHERE request_id=NEW.request_id; END;"
        "CREATE TRIGGER IF NOT EXISTS visual_audit_request_delete_v1 AFTER DELETE ON t_visualAuditV1 BEGIN "
        "DELETE FROM t_visualAuditItemV1 WHERE request_id=OLD.request_id;"
        "DELETE FROM t_visualAuditAlarmV1 WHERE request_id=OLD.request_id; END;"
        "CREATE TRIGGER IF NOT EXISTS visual_audit_alarm_delete_v1 AFTER DELETE ON t_commonEvent BEGIN "
        "DELETE FROM t_visualAuditV1 WHERE request_id IN "
        "(SELECT request_id FROM t_visualAuditAlarmV1 WHERE event_id=OLD.rec_id) "
        "AND NOT EXISTS(SELECT 1 FROM t_visualAuditAlarmV1 other "
        "WHERE other.request_id=t_visualAuditV1.request_id AND other.event_id<>OLD.rec_id);"
        "DELETE FROM t_visualAuditAlarmV1 WHERE event_id=OLD.rec_id; END;");
    transaction.Commit();
}

bool VisualAuditDao::Begin(const Json& request, const std::string& owner, int64_t createdMs) {
    const auto identity = RequestIdentity(request);
    Require(ValidId(owner) && createdMs >= 0);
    const auto id = identity.at("request_id").get<std::string>();
    ScopedDbSavepoint transaction(Db());
    SQLite::Statement insert(
        Db(), "INSERT OR IGNORE INTO t_visualAuditV1(request_id,owner,created_ms,request) VALUES(?,?,?,?)");
    insert.bind(1, id);
    insert.bind(2, owner);
    insert.bind(3, createdMs);
    insert.bind(4, identity.dump());
    if (insert.exec() == 0) {
        transaction.Commit();
        return false;
    }
    int ordinal = 0;
    for (const auto& item : identity.at("items")) {
        SQLite::Statement row(
            Db(), "INSERT INTO t_visualAuditItemV1(request_id,ordinal,item_id,identity) VALUES(?,?,?,?)");
        row.bind(1, id);
        row.bind(2, ordinal++);
        row.bind(3, item.at("item_id").get<std::string>());
        row.bind(4, item.dump());
        row.exec();
    }
    transaction.Commit();
    return true;
}

bool VisualAuditDao::Finish(const std::string& requestId, const Json& response, int64_t finishedMs) {
    Require(ValidId(requestId) && finishedMs >= 0);
    ScopedDbSavepoint transaction(Db());
    Json request;
    {
        SQLite::Statement pending(
            Db(), "SELECT request FROM t_visualAuditV1 WHERE request_id=? AND response IS NULL");
        pending.bind(1, requestId);
        if (!pending.executeStep()) {
            transaction.Commit();
            return false;
        }
        request = Json::parse(pending.getColumn(0).getString());
    }
    ValidateResponse(request, response);
    for (const auto& item : response.at("items")) {
        SQLite::Statement update(
            Db(),
            "UPDATE t_visualAuditItemV1 SET result=? WHERE request_id=? AND item_id=? AND result IS NULL");
        update.bind(1, item.dump());
        update.bind(2, requestId);
        update.bind(3, item.at("item_id").get<std::string>());
        Require(update.exec() == 1);
    }
    SQLite::Statement finish(
        Db(), "UPDATE t_visualAuditV1 SET response=?,finished_ms=? WHERE request_id=? AND response IS NULL");
    finish.bind(1, response.dump());
    finish.bind(2, finishedMs);
    finish.bind(3, requestId);
    Require(finish.exec() == 1);
    transaction.Commit();
    return true;
}

bool VisualAuditDao::Delivery(const std::string& requestId, const std::string& status) {
    Require(ValidId(requestId) &&
            (status == "returned" || status == "cancelled" || status == "no_alarm" ||
             status == "alarm_store_failed" || status == "publication_failed" || status == "filtered"));
    SQLite::Statement update(
        Db(), "UPDATE t_visualAuditV1 SET delivery=? WHERE request_id=? AND delivery='pending'");
    update.bind(1, status);
    update.bind(2, requestId);
    return update.exec() == 1;
}

size_t VisualAuditDao::RecoverInterrupted(const std::string& currentOwner, int64_t nowMs) {
    Require(ValidId(currentOwner) && nowMs >= 0);
    ScopedDbSavepoint transaction(Db());
    std::vector<Json> pending;
    {
        SQLite::Statement query(Db(),
                                "SELECT request FROM t_visualAuditV1 WHERE owner<>? AND response IS NULL");
        query.bind(1, currentOwner);
        while (query.executeStep())
            pending.push_back(Json::parse(query.getColumn(0).getString()));
    }
    for (const auto& request : pending)
        Finish(request.at("request_id").get<std::string>(), Interrupted(request), nowMs);
    SQLite::Statement update(
        Db(), "UPDATE t_visualAuditV1 SET delivery='interrupted' WHERE owner<>? AND delivery='pending'");
    update.bind(1, currentOwner);
    update.exec();
    transaction.Commit();
    return pending.size();
}

size_t VisualAuditDao::RecoverClosed(int64_t nowMs) {
    Require(nowMs >= 0);
    ScopedDbSavepoint transaction(Db());
    std::vector<Json> pending;
    {
        SQLite::Statement query(
            Db(), "SELECT request FROM t_visualAuditV1 WHERE delivery<>'pending' AND response IS NULL");
        while (query.executeStep())
            pending.push_back(Json::parse(query.getColumn(0).getString()));
    }
    for (const auto& request : pending)
        Finish(request.at("request_id").get<std::string>(), Interrupted(request, "audit_result_write_failed"),
               nowMs);
    transaction.Commit();
    return pending.size();
}

size_t VisualAuditDao::PruneUnlinked(int64_t olderThanMs, int keepNewest) {
    Require(olderThanMs >= 0 && keepNewest >= 0 && keepNewest <= 100000);
    ScopedDbSavepoint transaction(Db());
    constexpr const char* eligible =
        " response IS NOT NULL AND delivery<>'pending' "
        "AND NOT EXISTS(SELECT 1 FROM t_visualAuditAlarmV1 l WHERE l.request_id=t_visualAuditV1.request_id) ";
    SQLite::Statement expired(
        Db(), std::string("DELETE FROM t_visualAuditV1 WHERE ") + eligible + "AND created_ms<?");
    expired.bind(1, olderThanMs);
    size_t count = expired.exec();
    SQLite::Statement excess(Db(), std::string("DELETE FROM t_visualAuditV1 WHERE request_id IN (SELECT "
                                               "request_id FROM t_visualAuditV1 WHERE ") +
                                       eligible +
                                       "ORDER BY created_ms DESC,request_id DESC LIMIT -1 OFFSET ?)");
    excess.bind(1, keepNewest);
    count += excess.exec();
    transaction.Commit();
    return count;
}

void VisualAuditDao::LinkEvent(const std::string& eventId, const std::vector<std::string>& requestIds) {
    Require(ValidId(eventId) && requestIds.size() <= 4096);
    ScopedDbSavepoint transaction(Db());
    for (const auto& id : requestIds) {
        Require(ValidId(id));
        SQLite::Statement link(Db(),
                               "INSERT OR IGNORE INTO t_visualAuditAlarmV1(event_id,request_id) VALUES(?,?)");
        link.bind(1, eventId);
        link.bind(2, id);
        link.exec();
    }
    transaction.Commit();
}

nlohmann::json VisualAuditDao::Page(const std::string& eventId, const std::string& requestId, int page,
                                    int size) {
    Require((eventId.empty() || ValidId(eventId)) && (requestId.empty() || ValidId(requestId)));
    const int64_t offset = (static_cast<int64_t>(std::max(1, page)) - 1) * std::clamp(size, 1, 100);
    size                 = std::clamp(size, 1, 100);
    ScopedDbSavepoint transaction(Db());
    const std::string where =
        " WHERE (?='' OR a.request_id=?) AND (?='' OR EXISTS(SELECT 1 FROM t_visualAuditAlarmV1 l "
        "WHERE l.request_id=a.request_id AND l.event_id=?)) ";
    auto bind = [&](SQLite::Statement& statement) {
        statement.bind(1, requestId);
        statement.bind(2, requestId);
        statement.bind(3, eventId);
        statement.bind(4, eventId);
    };
    Json result = {{"schema", 1}, {"rows", Json::array()}, {"total", 0}};
    {
        SQLite::Statement count(Db(), "SELECT COUNT(*) FROM t_visualAuditV1 a" + where);
        bind(count);
        if (count.executeStep())
            result["total"] = count.getColumn(0).getInt64();
    }
    SQLite::Statement query(Db(),
                            "SELECT a.request_id,a.created_ms,a.finished_ms,a.request,a.response,a.delivery "
                            "FROM t_visualAuditV1 a" +
                                where + "ORDER BY a.created_ms DESC,a.request_id DESC LIMIT ? OFFSET ?");
    bind(query);
    query.bind(5, size);
    query.bind(6, offset);
    while (query.executeStep()) {
        const auto id = query.getColumn(0).getString();
        Json row      = {{"request_id", id},
                         {"created_ms", query.getColumn(1).getInt64()},
                         {"finished_ms",
                     query.getColumn(2).isNull() ? Json(nullptr) : Json(query.getColumn(2).getInt64())},
                         {"request", Json::parse(query.getColumn(3).getString())},
                         {"response", query.getColumn(4).isNull() ? Json(nullptr)
                                                                  : Json::parse(query.getColumn(4).getString())},
                         {"delivery", query.getColumn(5).getString()},
                         {"events", Json::array()},
                         {"items", Json::array()}};
        SQLite::Statement events(
            Db(), "SELECT event_id FROM t_visualAuditAlarmV1 WHERE request_id=? ORDER BY event_id");
        events.bind(1, id);
        while (events.executeStep())
            row["events"].push_back(events.getColumn(0).getString());
        SQLite::Statement items(
            Db(), "SELECT identity,result FROM t_visualAuditItemV1 WHERE request_id=? ORDER BY ordinal");
        items.bind(1, id);
        while (items.executeStep())
            row["items"].push_back(
                {{"identity", Json::parse(items.getColumn(0).getString())},
                 {"result", items.getColumn(1).isNull() ? Json(nullptr)
                                                        : Json::parse(items.getColumn(1).getString())}});
        result["rows"].push_back(std::move(row));
    }
    transaction.Commit();
    return result;
}

}  // namespace cosmo::db
