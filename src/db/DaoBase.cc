// DaoBase — Dao Base implementation.

#include "db/DaoBase.h"

#include <SQLiteCpp/SQLiteCpp.h>

#include <algorithm>

#include "util/Log.h"

namespace cosmo::db {

DaoBase::DaoBase(SQLite::Database& db) : db_(&db) {}

void DaoBase::Begin() {
    if (!is_transaction_active_) {
        db_->exec("BEGIN IMMEDIATE");
        is_transaction_active_ = true;
    }
}

void DaoBase::Commit() {
    if (is_transaction_active_) {
        db_->exec("COMMIT");
        is_transaction_active_ = false;
    }
}

void DaoBase::Rollback() {
    if (is_transaction_active_) {
        db_->exec("ROLLBACK");
        is_transaction_active_ = false;
    }
}

SQLite::Database& DaoBase::Db() {
    return *db_;
}

const SQLite::Database& DaoBase::Db() const {
    return *db_;
}

size_t DaoBase::QueryRows(const std::string& query_sql) {
    std::string count_sql = "SELECT COUNT(*) FROM (" + query_sql + ")";
    SQLite::Statement query(*db_, count_sql);
    if (query.executeStep()) {
        return static_cast<size_t>(query.getColumn(0).getInt());
    }
    return 0;
}

std::string& DaoBase::SetCondition(std::string& query_sql, std::vector<std::string>&& cond_vec) const {
    if (cond_vec.empty()) {
        return query_sql;
    }

    query_sql.append(" WHERE ");
    for (size_t i = 0; i < cond_vec.size(); i++) {
        if (i > 0) {
            query_sql.append(" AND ");
        }
        query_sql.append(cond_vec[i]);
    }
    return query_sql;
}

std::string& DaoBase::SetCondition(std::string& query_sql, const std::vector<std::string>& cond_vec) const {
    if (cond_vec.empty()) {
        return query_sql;
    }

    query_sql.append(" WHERE ");
    for (size_t i = 0; i < cond_vec.size(); i++) {
        if (i > 0) {
            query_sql.append(" AND ");
        }
        query_sql.append(cond_vec[i]);
    }
    return query_sql;
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
