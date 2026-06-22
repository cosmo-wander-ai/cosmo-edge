#pragma once

#include <string>

#include "util/LimitedType.h"

namespace SQLite {
class Statement;
}

namespace cosmo::db {

// Sequential column reader for SQLiteCpp Statement results.
//
// Maintains an internal column index that auto-increments on each SetValue() call,
// enabling compact sequential extraction of query result columns.
class RowFieldReader {
public:
    explicit RowFieldReader(SQLite::Statement& stmt);

    // Set value from next column (auto-incrementing index)
    void SetValue(std::string& value);
    void SetValue(int& value);
    void SetValue(int64_t& value);
    void SetValue(uint64_t& value);
    void SetValue(float& value);
    void SetValue(double& value);

    // ---- Template overloads for cosmo::util bounded types ----

    template <size_t MinSize, size_t MaxSize>
    void SetValue(cosmo::util::String<MinSize, MaxSize>& value) {
        std::string tmp;
        SetValue(tmp);
        value = std::move(tmp);
    }

    template <int MinValue, int MaxValue>
    void SetValue(cosmo::util::RangeInt<MinValue, MaxValue>& value) {
        int tmp = 0;
        SetValue(tmp);
        value = tmp;
    }

    template <typename T>
    void SetValue(cosmo::util::RangeValue<T>& value) {
        T tmp{};
        SetValue(tmp);
        value = tmp;
    }

    // Return current step index and advance (for manual column access)
    int Step();

private:
    SQLite::Statement& stmt_;
    int step_{0};
};

}  // namespace cosmo::db
