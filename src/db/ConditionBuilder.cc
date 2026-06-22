// ConditionBuilder — ConditionBuilder: type-safe parameterized SQL WHERE clause builder.

#include "db/ConditionBuilder.h"

#include <SQLiteCpp/SQLiteCpp.h>

namespace cosmo::db {

int ConditionBuilder::BindAll(SQLite::Statement& stmt, int start_index) const {
    int idx = start_index;
    for (const auto& frag : fragments_) {
        for (const auto& val : frag.values) {
            std::visit([&](const auto& v) { stmt.bind(idx, v); }, val);
            ++idx;
        }
    }
    return idx;
}

}  // namespace cosmo::db
