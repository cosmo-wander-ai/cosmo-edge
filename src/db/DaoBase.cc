// DaoBase — Dao Base implementation.

#include "db/DaoBase.h"

#include <SQLiteCpp/SQLiteCpp.h>
#include <sqlite3.h>

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "util/Log.h"

namespace cosmo::db {

DaoBase::ConnectionMutex::ConnectionMutex(sqlite3_mutex* mutex) : mutex_(mutex) {
    if (mutex_ == nullptr) {
        throw std::invalid_argument("DaoBase requires a serialized SQLite connection");
    }
}

void DaoBase::ConnectionMutex::lock() {
    sqlite3_mutex_enter(mutex_);
}

void DaoBase::ConnectionMutex::unlock() {
    sqlite3_mutex_leave(mutex_);
}

DaoBase::DaoBase(SQLite::Database& db) : db_(&db), connection_mutex_(sqlite3_db_mutex(db.getHandle())) {}

void DaoBase::Begin() {
    std::unique_lock<ConnectionMutex> connection_lock(connection_mutex_);
    if (is_transaction_active_) {
        return;
    }

    if (sqlite3_get_autocommit(db_->getHandle()) == 0) {
        throw std::logic_error("Nested transactions on a shared SQLite connection are not supported");
    }

    db_->exec("BEGIN IMMEDIATE");
    is_transaction_active_ = true;
    transaction_lock_      = std::move(connection_lock);
}

void DaoBase::Commit() {
    std::unique_lock<ConnectionMutex> connection_lock(connection_mutex_);
    if (!is_transaction_active_) {
        return;
    }

    db_->exec("COMMIT");
    is_transaction_active_ = false;
    transaction_lock_.unlock();
}

void DaoBase::Rollback() {
    std::unique_lock<ConnectionMutex> connection_lock(connection_mutex_);
    if (!is_transaction_active_) {
        return;
    }

    db_->exec("ROLLBACK");
    is_transaction_active_ = false;
    transaction_lock_.unlock();
}

SQLite::Database& DaoBase::Db() {
    return *db_;
}

const SQLite::Database& DaoBase::Db() const {
    return *db_;
}

void DaoBase::SetLimit(std::string& query_sql, int page_num, int page_size) const {
    if (page_size > 0 && page_num > 0) {
        query_sql.append(" LIMIT " + std::to_string(page_size) + " OFFSET " +
                         std::to_string((page_num - 1) * page_size));
    }
}

void DaoBase::DeleteItems(const std::string& table_name, const std::vector<std::string>& list) {
    if (list.empty()) {
        return;
    }

    // Build parameterized IN clause: DELETE FROM table WHERE rec_id IN (?,?,...)
    std::string delete_sql;
    delete_sql.reserve(256);
    delete_sql.append("DELETE FROM ").append(table_name).append(" WHERE rec_id IN (");
    for (size_t i = 0; i < list.size(); i++) {
        if (i > 0) {
            delete_sql.append(",");
        }
        delete_sql.append("?");
    }
    delete_sql.append(")");

    SQLite::Statement stmt(*db_, delete_sql);
    for (size_t i = 0; i < list.size(); i++) {
        stmt.bind(static_cast<int>(i + 1), list[i]);
    }
    stmt.exec();
}

std::vector<std::string> DaoBase::GetTableAllColumns(const std::string& table_name) const {
    std::vector<std::string> columns;
    std::string query_sql = "PRAGMA table_info(" + table_name + ")";
    SQLite::Statement query(*db_, query_sql);
    while (query.executeStep()) {
        columns.push_back(query.getColumn("name").getString());
    }
    return columns;
}

bool DaoBase::IsColumnExist(const std::vector<std::string>& columns, const std::string& column_name) const {
    return std::any_of(columns.begin(), columns.end(), [&](const auto& col) { return col == column_name; });
}

void DaoBase::AddColumnToTable(const std::string& table_name, const std::string& column_name,
                               ColumnType type) {
    std::string type_str;
    switch (type) {
        case ColumnType::BOOLEAN:
        case ColumnType::INTEGER:
            type_str = "INTEGER";
            break;
        case ColumnType::INT64:
            type_str = "INTEGER";
            break;
        case ColumnType::FLOAT:
            type_str = "REAL";
            break;
        case ColumnType::TEXT:
            type_str = "TEXT";
            break;
        case ColumnType::BLOB:
            type_str = "BLOB";
            break;
    }

    std::string alter_sql = "ALTER TABLE " + table_name + " ADD COLUMN " + column_name + " " + type_str;
    db_->exec(alter_sql);
}

}  // namespace cosmo::db
