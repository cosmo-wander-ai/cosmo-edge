#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "db/DaoBase.h"

namespace cosmo::db {

// Versioned numerical-decision audit in the SAME database as t_commonEvent.
// One request is one ROI and up to eight questions. Events and requests have a
// many-to-many relationship (associated flows may reuse a result). Linked rows
// follow real alarm deletion, never an independent "last 10000 reviews" limit.
// This DAO does not grant model qualification or authorize alarm filtering.
class VisualAuditDao : public DaoBase {
public:
    explicit VisualAuditDao(SQLite::Database& db) : DaoBase(db) {}
    // Call after TaskEventDao has created t_commonEvent. Reopening is read-only
    // with respect to existing results; only explicit startup recovery changes them.
    void CreateTable();
    bool Begin(const nlohmann::json& request, const std::string& owner, int64_t createdMs);
    // Immutable first completion of the entire batch, including each item's identity.
    // Returns false for unknown/deleted/finished IDs; malformed identities throw.
    bool Finish(const std::string& requestId, const nlohmann::json& response, int64_t finishedMs);
    bool Delivery(const std::string& requestId, const std::string& status);
    // Called once at engine startup, before inference starts. Never marks the
    // current owner's work interrupted and never rewrites a completed result.
    size_t RecoverInterrupted(const std::string& currentOwner, int64_t nowMs);
    // A terminal delivery means the last producer is done. If its final write
    // failed, keep an explicit unknown result instead of leaking a pending row.
    size_t RecoverClosed(int64_t nowMs);
    // Standalone picture/no-alarm/cancelled records only. Active pending records
    // and every record still linked to a retained alarm are excluded.
    size_t PruneUnlinked(int64_t olderThanMs, int keepNewest = 10000);
    // Alarm must already exist. Caller uses TaskEventDao::Insert(data, ids) for
    // atomic alarm + links. Unknown audit IDs fail the entire transaction.
    void LinkEvent(const std::string& eventId, const std::vector<std::string>& requestIds);
    nlohmann::json Page(const std::string& eventId = "", const std::string& requestId = "", int page = 1,
                        int size = 20);
};

}  // namespace cosmo::db
