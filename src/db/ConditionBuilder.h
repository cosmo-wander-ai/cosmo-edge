// ConditionBuilder: type-safe parameterized SQL WHERE clause builder.
// Eliminates SQL injection risk from string concatenation in DAO queries.

#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace SQLite {
class Statement;
}

namespace cosmo::db {

// Supported bind value types for parameterized conditions.
using BindValue = std::variant<std::string, int, int64_t, double>;

// A single condition fragment with its associated bind values.
// Example: fragment = "area_id = ?", values = {"some_id"}
struct ConditionFragment {
    std::string fragment;
    std::vector<BindValue> values;
};

// Builds parameterized WHERE clauses with bind values.
// Usage:
//   ConditionBuilder cb;
//   cb.AddEqual("area_id", condition.areaId);
//   cb.AddGreaterOrEqual("create_time", condition.timeBegin);
//   cb.AddIn("algorithm_code", codes);
//
//   auto where_sql = cb.BuildWhereClause();   // " WHERE area_id = ? AND ..."
//   SQLite::Statement stmt(db, select_sql + where_sql);
//   cb.BindAll(stmt);                         // binds all values
class ConditionBuilder {
public:
    ConditionBuilder() = default;

    // --- Condition adders (only add if value is non-empty / non-zero) ---

    // column = ?
    void AddEqual(const std::string& column, const std::string& value) {
        if (!value.empty()) {
            fragments_.push_back({column + " = ?", {value}});
        }
    }

    void AddEqual(const std::string& column, int value) {
        fragments_.push_back({column + " = ?", {value}});
    }

    // column >= ?
    void AddGreaterOrEqual(const std::string& column, int64_t value) {
        if (value > 0) {
            fragments_.push_back({column + " >= ?", {value}});
        }
    }

    // column < ?
    void AddLessThan(const std::string& column, int64_t value) {
        if (value > 0) {
            fragments_.push_back({column + " < ?", {value}});
        }
    }

    // column LIKE '%value%'  (parameterized)
    void AddLike(const std::string& column, const std::string& value) {
        if (!value.empty()) {
            fragments_.push_back({column + " LIKE ?", {"%" + value + "%"}});
        }
    }

    // column IN (?, ?, ...)
    void AddIn(const std::string& column, const std::vector<std::string>& values) {
        if (values.empty())
            return;
        std::string placeholders;
        std::vector<BindValue> bind_values;
        for (size_t i = 0; i < values.size(); ++i) {
            if (i > 0)
                placeholders += ",";
            placeholders += "?";
            bind_values.push_back(values[i]);
        }
        fragments_.push_back({column + " IN (" + placeholders + ")", std::move(bind_values)});
    }

    // (column = ? OR column = ? OR ...) — for multi-value OR conditions
    void AddOr(const std::string& column, const std::vector<std::string>& values) {
        if (values.empty())
            return;
        // Use IN clause instead of multiple ORs for cleaner SQL
        AddIn(column, values);
    }

    // Raw fragment with no bind values (e.g., pre-built safe sub-clauses)
    void AddRaw(const std::string& fragment) {
        if (!fragment.empty()) {
            fragments_.push_back({fragment, {}});
        }
    }

    // Check if any conditions have been added
    [[nodiscard]] bool Empty() const {
        return fragments_.empty();
    }

    // Build " WHERE cond1 AND cond2 ..." string. Returns empty string if no conditions.
    [[nodiscard]] std::string BuildWhereClause() const {
        if (fragments_.empty())
            return {};
        std::string sql = " WHERE ";
        for (size_t i = 0; i < fragments_.size(); ++i) {
            if (i > 0)
                sql += " AND ";
            sql += fragments_[i].fragment;
        }
        return sql;
    }

    // Append WHERE clause to an existing SQL string. Returns reference for chaining.
    std::string& AppendTo(std::string& sql) const {
        sql += BuildWhereClause();
        return sql;
    }

    // Bind all collected values to a prepared statement.
    // start_index: 1-based starting bind index (default 1).
    // Returns the next available bind index.
    int BindAll(SQLite::Statement& stmt, int start_index = 1) const;

private:
    std::vector<ConditionFragment> fragments_;
};

}  // namespace cosmo::db
