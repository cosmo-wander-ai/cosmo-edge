#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

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

    void Begin();
    void Commit();
    void Rollback();

    [[nodiscard]] SQLite::Database& Db();
    [[nodiscard]] const SQLite::Database& Db() const;

    // Retrieve the total row count produced by a given SELECT query string
    [[nodiscard]] size_t QueryRows(const std::string& query_sql);

protected:
    // Helper: build WHERE clause from a vector of condition strings and append
    std::string& SetCondition(std::string& query_sql, std::vector<std::string>&& cond_vec) const;
    std::string& SetCondition(std::string& query_sql, const std::vector<std::string>& cond_vec) const;

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
    SQLite::Database* db_;
    bool is_transaction_active_{false};
};

}  // namespace cosmo::db
