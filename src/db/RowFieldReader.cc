// RowFieldReader — Sequential column reader for SQLiteCpp Statement results.

#include "db/RowFieldReader.h"

#include <SQLiteCpp/SQLiteCpp.h>

namespace cosmo::db {

RowFieldReader::RowFieldReader(SQLite::Statement& stmt) : stmt_(stmt) {}

void RowFieldReader::SetValue(std::string& value) {
    if (stmt_.isColumnNull(step_)) {
        value.clear();
    } else {
        value = stmt_.getColumn(step_).getString();
    }
    step_++;
}

void RowFieldReader::SetValue(int& value) {
    if (stmt_.isColumnNull(step_)) {
        value = 0;
    } else {
        value = stmt_.getColumn(step_).getInt();
    }
    step_++;
}

void RowFieldReader::SetValue(int64_t& value) {
    if (stmt_.isColumnNull(step_)) {
        value = 0;
    } else {
        value = stmt_.getColumn(step_).getInt64();
    }
    step_++;
}

void RowFieldReader::SetValue(uint64_t& value) {
    if (stmt_.isColumnNull(step_)) {
        value = 0;
    } else {
        value = static_cast<uint64_t>(stmt_.getColumn(step_).getInt64());
    }
    step_++;
}

void RowFieldReader::SetValue(float& value) {
    if (stmt_.isColumnNull(step_)) {
        value = 0.0f;
    } else {
        value = static_cast<float>(stmt_.getColumn(step_).getDouble());
    }
    step_++;
}

void RowFieldReader::SetValue(double& value) {
    if (stmt_.isColumnNull(step_)) {
        value = 0.0;
    } else {
        value = stmt_.getColumn(step_).getDouble();
    }
    step_++;
}

int RowFieldReader::Step() {
    return step_++;
}

}  // namespace cosmo::db