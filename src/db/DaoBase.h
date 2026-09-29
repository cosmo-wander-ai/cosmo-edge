#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <variant>
#include <vector>

struct sqlite3_mutex;

namespace SQLite {
class Database;
}

namespace cosmo::db {

class DaoBase {
public:
    enum class ColumnType { BOOLEAN, INTEGER, INT64, FLOAT, TEXT, BLOB };

    // Construct with a reference to an SQLite::Database
    explicit DaoBase(SQLite::Database& db);

    virtual ~DaoBase() = default;

    DaoBase(const DaoBase&)            = delete;
    DaoBase& operator=(const DaoBase&) = delete;

    void Begin();
    void Commit();
    void Rollback();

    [[nodiscard]] SQLite::Database& Db();
    [[nodiscard]] const SQLite::Database& Db() const;

protected:
    // Helper: append LIMIT/OFFSET clause
    void SetLimit(std::string& query_sql, int page_num, int page_size) const;

    // Helper: delete items from a table by rec_id list (parameterized)
    void DeleteItems(const std::string& table_name, const std::vector<std::string>& list);

    // Column existence helpers
    [[nodiscard]] std::vector<std::string> GetTableAllColumns(const std::string& table_name) const;
    [[nodiscard]] bool IsColumnExist(const std::vector<std::string>& columns,
                                     const std::string& column_name) const;
    void AddColumnToTable(const std::string& table_name, const std::string& column_name, ColumnType type);

private:
    class ConnectionMutex {
    public:
        explicit ConnectionMutex(sqlite3_mutex* mutex);

        void lock();
        void unlock();

    private:
        sqlite3_mutex* mutex_;
    };

    SQLite::Database* db_;
    ConnectionMutex connection_mutex_;
    std::unique_lock<ConnectionMutex> transaction_lock_;
    bool is_transaction_active_{false};
};

}  // namespace cosmo::db
