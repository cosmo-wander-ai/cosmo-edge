// PersonRecogDao — Person Recog Dao implementation.

#include "db/PersonRecogDao.h"

#include <SQLiteCpp/SQLiteCpp.h>

#include <chrono>

#include "db/ConditionBuilder.h"
#include "db/RowFieldReader.h"
#include "util/DurationLogger.h"
#include "util/Log.h"
#include "util/StringUtil.h"
#include "util/TimeUtil.h"

namespace cosmo::db {

PersonRecogDao::PersonRecogDao(SQLite::Database& db) : DaoBase(db) {}

void PersonRecogDao::CreateTable() {
    std::string create_sql;
    create_sql.reserve(1024);

    // Body library table
    create_sql.append(
        "create table "
        "if not exists "
        "t_person_lib "
        "("
        "person_lib_id   TEXT  PRIMARY KEY,"
        "lib_name        TEXT,"
        "lib_type        INTEGER,"
        "threshold       TEXT,"
        "capacity        TEXT,"
        "modify_time     INTEGER,"
        "create_time     INTEGER)");
    Db().exec(create_sql);
    create_sql.clear();

    // Person table
    create_sql.append(
        "create table "
        "if not exists "
        "t_person_feature("
        "person_id       TEXT  PRIMARY KEY,"
        "person_lib_id   TEXT,"
        "picture_name    TEXT,"
        "person_feature  BLOB,"
        "modify_time     INTEGER,"
        "create_time     INTEGER)");
    Db().exec(create_sql);
    create_sql.clear();
}

bool PersonRecogDao::ClearFeature() {
    bool ret = false;
    {
        // Create a new table based on old table without person_feature
        std::string op_sql =
            "create table t_person_feature_temp as select "
            "person_id,person_lib_id,picture_name,modify_time,create_time from t_person_feature";
        ret = Db().exec(op_sql) > 0;
        LOG_INFO("Exec {} Get [{}]", op_sql, ret);

        // Drop old table
        op_sql = "drop table t_person_feature";
        ret    = Db().exec(op_sql) > 0;
        LOG_INFO("Exec {} Get [{}]", op_sql, ret);

        // Rename table
        op_sql = "alter table t_person_feature_temp rename to t_person_feature";
        ret    = Db().exec(op_sql) > 0;
        LOG_INFO("Exec {} Get [{}]", op_sql, ret);

        // Re-add person_feature column
        op_sql = "alter table t_person_feature add person_feature BLOB";
        ret    = Db().exec(op_sql) > 0;
        LOG_INFO("Exec {} Get [{}]", op_sql, ret);
    }
    return ret;
}

PersonRecogQueryResult PersonRecogDao::QueryPersons(const PersonRecogQueryCondition& condition) const {
    // Build parameterized conditions
    ConditionBuilder cb;
    cb.AddIn("person_lib_id", condition.person_lib_id_list);
    cb.AddEqual("person_id", condition.person_id);
    cb.AddLike("picture_name", condition.picture_name);

    // Query statement
    std::string base_sql = "select person_id from t_person_feature";
    auto where_clause    = cb.BuildWhereClause();

    // Count
    std::string count_sql = base_sql + where_clause;

    std::string query_sql;
    query_sql.reserve(1024);
    query_sql.append(
        " select"
        " person_id, person_lib_id, picture_name, modify_time,create_time, person_feature "
        " from t_person_feature ");
    query_sql += where_clause;

    PersonRecogQueryResult result{};
    util::DurationLogger loggerCount("PersonPictures count");
    // Count total records with parameterized query
    std::string count_wrap = "SELECT COUNT(*) FROM (" + count_sql + ")";
    SQLite::Statement count_stmt(Db(), count_wrap);
    cb.BindAll(count_stmt);
    if (count_stmt.executeStep()) {
        result.total_count = static_cast<size_t>(count_stmt.getColumn(0).getInt());
    }
    loggerCount.Print();

    SetLimit(query_sql, condition.page_num, condition.page_size);
    util::DurationLogger loggerQuery("PersonPictures query");
    SQLite::Statement query(Db(), query_sql);
    cb.BindAll(query);
    loggerQuery.Print();
    while (query.executeStep()) {
        PersonRecogRecord record{};

        RowFieldReader row(query);
        row.SetValue(record.id);
        row.SetValue(record.person_lib.id);
        row.SetValue(record.picture_name);
        record.update_timestamp = query.getColumn(row.Step()).getInt64();
        record.create_timestamp = query.getColumn(row.Step()).getInt64();

        auto col         = query.getColumn(row.Step());
        auto feature_str = reinterpret_cast<const float*>(col.getBlob());
        int length       = col.getBytes();
        record.feature.assign(feature_str, feature_str + length / 4);

        result.person_list.push_back(std::move(record));
    }

    return result;
}

PersonRecogLibQueryResult PersonRecogDao::QueryPersonLib(
    const PersonRecogLibQueryCondition& condition) const {
    // Build parameterized conditions
    ConditionBuilder cb;
    cb.AddLike("personset_name", condition.person_lib_name);
    if (condition.person_lib_type > static_cast<int>(PersonRecogType::None)) {
        cb.AddEqual("fs.lib_type", condition.person_lib_type);
    }

    // Query statement
    std::string count_base = "select fs.lib_name as personset_name from t_person_lib fs";
    auto where_clause      = cb.BuildWhereClause();
    std::string count_sql  = count_base + where_clause;

    std::string query_sql;
    query_sql.reserve(1024);
    query_sql.append(
        " select"
        " fs.person_lib_id,fs.lib_name as personset_name,"
        " fs.lib_type,fs.threshold,fs.capacity,fs.modify_time,fs.create_time,"
        " count(distinct fsp.person_id) as person_count "
        " from t_person_lib fs"
        " left join t_person_feature fsp on fsp.person_lib_id=fs.person_lib_id");
    query_sql += where_clause;
    query_sql.append(" group by fs.person_lib_id order by fs.create_time desc");

    PersonRecogLibQueryResult result{};
    util::DurationLogger loggerCount("PersonFeatureLib count");
    // Count with parameterized query
    std::string count_wrap = "SELECT COUNT(*) FROM (" + count_sql + ")";
    SQLite::Statement count_stmt(Db(), count_wrap);
    cb.BindAll(count_stmt);
    if (count_stmt.executeStep()) {
        result.person_lib_count = static_cast<size_t>(count_stmt.getColumn(0).getInt());
    }
    loggerCount.Print();

    SetLimit(query_sql, condition.page_num, condition.page_size);
    util::DurationLogger loggerQuery("PersonFeatureLib query");
    SQLite::Statement query(Db(), query_sql);
    cb.BindAll(query);
    loggerQuery.Print();
    while (query.executeStep()) {
        PersonRecogLibRecord record{};

        RowFieldReader row(query);
        row.SetValue(record.id);
        row.SetValue(record.name);
        row.SetValue(record.type);
        record.threshold         = query.getColumn(row.Step()).getDouble();
        record.max_person_number = query.getColumn(row.Step()).getInt();
        record.update_timestamp  = query.getColumn(row.Step()).getInt64();
        record.create_timestamp  = query.getColumn(row.Step()).getInt64();
        row.SetValue(record.person_number);

        result.person_lib_list.push_back(std::move(record));
    }

    return result;
}

std::vector<float> PersonRecogDao::QueryPersonFeature(const std::string& person_id) const {
    std::vector<float> result;

    SQLite::Statement query(Db(), "SELECT person_feature FROM t_person_feature WHERE person_id=?");
    query.bind(1, person_id);
    if (query.executeStep()) {
        auto col         = query.getColumn(0);
        auto feature_str = reinterpret_cast<const float*>(col.getBlob());
        int length       = col.getBytes();
        result.assign(feature_str, feature_str + length / 4);
    }

    return result;
}

bool PersonRecogDao::AddPersonLib(const LibInfo& data) {
    auto now_time = util::GetMilliseconds();

    SQLite::Statement stmt(Db(),
                           "insert into t_person_lib ("
                           "person_lib_id, lib_name, lib_type, threshold, capacity, modify_time, create_time"
                           ") values (?,?,?,?,?,?,?)");
    stmt.bind(1, data.id);
    stmt.bind(2, data.name);
    stmt.bind(3, data.type);
    stmt.bind(4, std::to_string(data.threshold));
    stmt.bind(5, std::to_string(data.max_capacity));
    stmt.bind(6, static_cast<int64_t>(now_time));
    stmt.bind(7, static_cast<int64_t>(now_time));

    return stmt.exec() > 0;
}

bool PersonRecogDao::UpdatePersonLib(const LibInfo& data) {
    auto now_time = util::GetMilliseconds();

    SQLite::Statement stmt(
        Db(),
        "update t_person_lib set lib_name=?,lib_type=?,capacity=?,threshold=?,modify_time=? "
        "where person_lib_id=?");
    stmt.bind(1, data.name);
    stmt.bind(2, data.type);
    stmt.bind(3, std::to_string(data.max_capacity));
    stmt.bind(4, std::to_string(data.threshold));
    stmt.bind(5, static_cast<int64_t>(now_time));
    stmt.bind(6, data.id);

    return stmt.exec() > 0;
}

bool PersonRecogDao::RemovePersonLib(const std::string& person_lib_id) {
    // Clear body library data
    ClearPersonLib(person_lib_id);
    // Delete body library
    SQLite::Statement stmt(Db(), "DELETE FROM t_person_lib WHERE person_lib_id=?");
    stmt.bind(1, person_lib_id);
    return stmt.exec() > 0;
}

bool PersonRecogDao::ClearPersonLib(const std::string& person_lib_id) {
    // Delete all persons in this library
    SQLite::Statement stmt(Db(), "DELETE FROM t_person_feature WHERE person_lib_id=?");
    stmt.bind(1, person_lib_id);
    LOG_INFO("ClearPersonLib {}", person_lib_id);
    return stmt.exec() > 0;
}

bool PersonRecogDao::AddPerson(const std::string& person_id, const std::string& person_lib_id,
                               const std::string& person_name, const std::vector<float>& feature) {
    auto now_time = util::GetMilliseconds();

    SQLite::Statement stmt(Db(),
                           "insert into t_person_feature ("
                           "person_id, person_lib_id, picture_name, person_feature, modify_time, create_time"
                           ") values (?,?,?,?,?,?)");
    stmt.bind(1, person_id);
    stmt.bind(2, person_lib_id);
    stmt.bind(3, person_name);
    stmt.bind(4, reinterpret_cast<const void*>(feature.data()), static_cast<int>(feature.size() * 4));
    stmt.bind(5, static_cast<int64_t>(now_time));
    stmt.bind(6, static_cast<int64_t>(now_time));
    LOG_INFO("Insert Person {} To Db, Feature Size is:{}", person_id, feature.size());
    if (stmt.exec() <= 0) {
        return false;
    }
    return true;
}

bool PersonRecogDao::UpdatePerson(const std::string& person_id, const std::string& person_name) {
    auto now_time = util::GetMilliseconds();

    SQLite::Statement stmt(Db(),
                           "update t_person_feature set picture_name=?,modify_time=? where person_id=?");
    stmt.bind(1, person_name);
    stmt.bind(2, static_cast<int64_t>(now_time));
    stmt.bind(3, person_id);

    return stmt.exec() > 0;
}

bool PersonRecogDao::UpdateFeature(const std::string& person_id, const std::vector<float>& feature) {
    auto now_time = util::GetMilliseconds();

    SQLite::Statement stmt(Db(),
                           "update t_person_feature set modify_time=?, person_feature=? where person_id=?");
    stmt.bind(1, static_cast<int64_t>(now_time));
    stmt.bind(2, reinterpret_cast<const void*>(feature.data()), static_cast<int>(feature.size() * 4));
    stmt.bind(3, person_id);

    if (stmt.exec() <= 0) {
        LOG_WARN("Person {} Update Feature failed.", person_id);
        return false;
    }
    return true;
}

bool PersonRecogDao::RemovePerson(const std::string& person_id) {
    // Delete person record
    SQLite::Statement stmt(Db(), "DELETE FROM t_person_feature WHERE person_id=?");
    stmt.bind(1, person_id);
    return stmt.exec() > 0;
}

std::vector<std::string> PersonRecogDao::GetAllPersonLibs() const {
    std::vector<std::string> result;
    // Query statement
    std::string query_sql;
    query_sql.reserve(1024);
    query_sql.append("select person_lib_id from t_person_lib");

    SQLite::Statement query(Db(), query_sql);
    while (query.executeStep()) {
        std::string faceset_token;

        RowFieldReader row(query);
        row.SetValue(faceset_token);
        result.emplace_back(std::move(faceset_token));
    }

    return result;
}

}  // namespace cosmo::db
