#pragma once

#include <SQLiteCpp/SQLiteCpp.h>
#include <sqlite3.h>

#include <stdexcept>

namespace cosmo::db {

// FULLMUTEX only serializes individual SQLite calls. Hold the connection mutex
// across the whole savepoint, including rollback, so other DAO threads cannot
// accidentally join this transaction. Savepoints also compose with DaoBase::Begin.
class ScopedDbSavepoint {
public:
    explicit ScopedDbSavepoint(SQLite::Database& db) : db_(db), mutex_(sqlite3_db_mutex(db.getHandle())) {
        if (!mutex_)
            throw std::invalid_argument("audit requires a serialized SQLite connection");
        sqlite3_mutex_enter(mutex_);
        try {
            ownsTransaction_ = sqlite3_get_autocommit(db_.getHandle()) != 0;
            db_.exec("SAVEPOINT cosmo_visual_audit");
        } catch (...) {
            sqlite3_mutex_leave(mutex_);
            throw;
        }
    }
    ~ScopedDbSavepoint() {
        if (!committed_) {
            try {
                if (ownsTransaction_) {
                    // A failed outer RELEASE may retain a pending writer lock.
                    // ROLLBACK TO + RELEASE retries the blocked commit, so end
                    // the transaction outright when this scope started it.
                    db_.exec("ROLLBACK");
                } else {
                    db_.exec("ROLLBACK TO cosmo_visual_audit");
                    db_.exec("RELEASE cosmo_visual_audit");
                }
            } catch (...) {
                // The original SQLite error must reach the caller.
            }
        }
        sqlite3_mutex_leave(mutex_);
    }
    void Commit() {
        db_.exec("RELEASE cosmo_visual_audit");
        committed_ = true;
    }
    ScopedDbSavepoint(const ScopedDbSavepoint&)            = delete;
    ScopedDbSavepoint& operator=(const ScopedDbSavepoint&) = delete;

private:
    SQLite::Database& db_;
    sqlite3_mutex* mutex_;
    bool committed_{false};
    bool ownsTransaction_{false};
};

}  // namespace cosmo::db
