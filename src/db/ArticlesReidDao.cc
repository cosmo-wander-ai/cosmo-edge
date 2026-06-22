// ArticlesReidDao — Articles Reid Dao implementation.

#include "db/ArticlesReidDao.h"

#include <SQLiteCpp/SQLiteCpp.h>

#include <chrono>

#include "db/ConditionBuilder.h"
#include "db/RowFieldReader.h"
#include "util/DurationLogger.h"
#include "util/Log.h"
#include "util/StringUtil.h"
#include "util/TimeUtil.h"

namespace cosmo::db {

ArticlesReidDao::ArticlesReidDao(SQLite::Database& db) : DaoBase(db) {}

void ArticlesReidDao::CreateTable() {
    std::string create_sql;
    create_sql.reserve(1024);

    // Things library table
    create_sql.append(
        "create table "
        "if not exists "
        "t_articlesreid_lib "
        "("
        "articlesreid_lib_id   TEXT  PRIMARY KEY,"
        "lib_name        TEXT,"
        "lib_type        INTEGER,"
        "threshold       TEXT,"
        "capacity        TEXT,"
        "modify_time     INTEGER,"
        "create_time     INTEGER)");
    Db().exec(create_sql);
    create_sql.clear();

    // Things feature table
    create_sql.append(
        "create table "
        "if not exists "
        "t_articlesreid_feature("
        "articlesreid_id       TEXT  PRIMARY KEY,"
        "articlesreid_lib_id   TEXT,"
        "picture_name    TEXT,"
        "articlesreid_feature  BLOB,"
        "modify_time     INTEGER,"
        "create_time     INTEGER)");
    Db().exec(create_sql);
    create_sql.clear();
}

bool ArticlesReidDao::ClearFeature() {
    bool ret = false;
    {
        // Create a new table based on old table without articlesreid_feature
        std::string op_sql =
            "create table t_articlesreid_feature_temp as select "
            "articlesreid_id,articlesreid_lib_id,picture_name,modify_time,create_time from "
            "t_articlesreid_feature";
        ret = Db().exec(op_sql) > 0;
        LOG_INFO("Exec {} Get [{}]", op_sql, ret);

        // Drop old table
        op_sql = "drop table t_articlesreid_feature";
        ret    = Db().exec(op_sql) > 0;
        LOG_INFO("Exec {} Get [{}]", op_sql, ret);

        // Rename table
        op_sql = "alter table t_articlesreid_feature_temp rename to t_articlesreid_feature";
        ret    = Db().exec(op_sql) > 0;
        LOG_INFO("Exec {} Get [{}]", op_sql, ret);

        // Re-add articlesreid_feature column
        op_sql = "alter table t_articlesreid_feature add articlesreid_feature BLOB";
        ret    = Db().exec(op_sql) > 0;
        LOG_INFO("Exec {} Get [{}]", op_sql, ret);
    }
    return ret;
}

ThingsQueryResult ArticlesReidDao::QueryThings(const ThingsQueryCondition& condition) const {
    // Build parameterized conditions
    ConditionBuilder cb;
    cb.AddIn("articlesreid_lib_id", condition.things_lib_id_list);
    cb.AddEqual("articlesreid_id", condition.things_id);
    // NOTE: picture_name column is "picture_name" but query references different table alias
    cb.AddLike("picture_name", condition.picture_name);

    // Query statement
    std::string count_base = "select articlesreid_id from t_articlesreid_feature";
    auto where_clause      = cb.BuildWhereClause();
    std::string count_sql  = count_base + where_clause;

    std::string query_sql;
    query_sql.reserve(1024);
    query_sql.append(
        " select"
        " articlesreid_id, articlesreid_lib_id, picture_name, modify_time,create_time, articlesreid_feature "
        " from t_articlesreid_feature ");
    query_sql += where_clause;

    ThingsQueryResult result{};
    util::DurationLogger logger_count("ArticlesReidPictures count");
    // Count with parameterized query
    std::string count_wrap = "SELECT COUNT(*) FROM (" + count_sql + ")";
    SQLite::Statement count_stmt(Db(), count_wrap);
    cb.BindAll(count_stmt);
    if (count_stmt.executeStep()) {
        result.total_count = static_cast<size_t>(count_stmt.getColumn(0).getInt());
    }
    logger_count.Print();

    SetLimit(query_sql, condition.page_num, condition.page_size);
    util::DurationLogger logger_query("ArticlesReidPictures query");
    SQLite::Statement query(Db(), query_sql);
    cb.BindAll(query);
    logger_query.Print();
    while (query.executeStep()) {
        ThingsRecord record{};

        RowFieldReader row(query);
        row.SetValue(record.id);
        row.SetValue(record.things_lib.id);
        row.SetValue(record.picture_name);
        record.update_timestamp = query.getColumn(row.Step()).getInt64();
        record.create_timestamp = query.getColumn(row.Step()).getInt64();

        auto col         = query.getColumn(row.Step());
        auto feature_str = reinterpret_cast<const float*>(col.getBlob());
        int length       = col.getBytes();
        record.feature.assign(feature_str, feature_str + length / 4);

        result.things_list.push_back(std::move(record));
    }

    return result;
}

ThingsLibQueryResult ArticlesReidDao::QueryThingsLib(const ThingsLibQueryCondition& condition) const {
    // Build parameterized conditions
    ConditionBuilder cb;
    cb.AddLike("articlesreidset_name", condition.things_lib_name);
    if (condition.things_lib_type > static_cast<int>(ArticlesReidType::None)) {
        cb.AddEqual("fs.lib_type", condition.things_lib_type);
    }

    // Query statement
    std::string count_base = "select fs.lib_name as articlesreidset_name from t_articlesreid_lib fs";
    auto where_clause      = cb.BuildWhereClause();
    std::string count_sql  = count_base + where_clause;

    std::string query_sql;
    query_sql.reserve(1024);
    query_sql.append(
        " select"
        " fs.articlesreid_lib_id,fs.lib_name as articlesreidset_name,"
        " fs.lib_type,fs.threshold,fs.capacity,fs.modify_time,fs.create_time,"
        " count(distinct fsp.articlesreid_id) as articlesreid_count "
        " from t_articlesreid_lib fs"
        " left join t_articlesreid_feature fsp on fsp.articlesreid_lib_id=fs.articlesreid_lib_id");
    query_sql += where_clause;
    query_sql.append(" group by fs.articlesreid_lib_id order by fs.create_time desc");

    ThingsLibQueryResult result{};
    util::DurationLogger logger_count("ArticlesReidFeatureLib count");
    // Count with parameterized query
    std::string count_wrap = "SELECT COUNT(*) FROM (" + count_sql + ")";
    SQLite::Statement count_stmt(Db(), count_wrap);
    cb.BindAll(count_stmt);
    if (count_stmt.executeStep()) {
        result.things_lib_count = static_cast<size_t>(count_stmt.getColumn(0).getInt());
    }
    logger_count.Print();

    SetLimit(query_sql, condition.page_num, condition.page_size);
    util::DurationLogger logger_query("ArticlesReidFeatureLib query");
    SQLite::Statement query(Db(), query_sql);
    cb.BindAll(query);
    logger_query.Print();
    while (query.executeStep()) {
        ThingsLibRecord record{};

        RowFieldReader row(query);
        row.SetValue(record.id);
        row.SetValue(record.name);
        row.SetValue(record.type);
        record.threshold         = query.getColumn(row.Step()).getDouble();
        record.max_things_number = query.getColumn(row.Step()).getInt();
        record.update_timestamp  = query.getColumn(row.Step()).getInt64();
        record.create_timestamp  = query.getColumn(row.Step()).getInt64();
        row.SetValue(record.things_number);

        result.things_lib_list.push_back(std::move(record));
    }

    return result;
}

std::vector<float> ArticlesReidDao::QueryArticlesReidFeature(const std::string& articles_reid_id) const {
    std::vector<float> result;

    SQLite::Statement query(
        Db(), "SELECT articlesreid_feature FROM t_articlesreid_feature WHERE articlesreid_id=?");
    query.bind(1, articles_reid_id);
    if (query.executeStep()) {
        auto col         = query.getColumn(0);
        auto feature_str = reinterpret_cast<const float*>(col.getBlob());
        int length       = col.getBytes();
        result.assign(feature_str, feature_str + length / 4);
    }

    return result;
}

bool ArticlesReidDao::AddArticlesReidLib(const LibInfo& data) {
    auto now_time = util::GetMilliseconds();

    SQLite::Statement stmt(
        Db(),
        "insert into t_articlesreid_lib ("
        "articlesreid_lib_id, lib_name, lib_type, threshold, capacity, modify_time, create_time"
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

bool ArticlesReidDao::UpdateArticlesReidLib(const LibInfo& data) {
    auto now_time = util::GetMilliseconds();

    SQLite::Statement stmt(
        Db(),
        "update t_articlesreid_lib set lib_name=?,lib_type=?,capacity=?,threshold=?,modify_time=? "
        "where articlesreid_lib_id=?");
    stmt.bind(1, data.name);
    stmt.bind(2, data.type);
    stmt.bind(3, std::to_string(data.max_capacity));
    stmt.bind(4, std::to_string(data.threshold));
    stmt.bind(5, static_cast<int64_t>(now_time));
    stmt.bind(6, data.id);

    return stmt.exec() > 0;
}

bool ArticlesReidDao::RemoveArticlesReidLib(const std::string& articles_reid_lib_id) {
    // Clear things library data
    ClearArticlesReidLib(articles_reid_lib_id);
    // Delete things library
    SQLite::Statement stmt(Db(), "DELETE FROM t_articlesreid_lib WHERE articlesreid_lib_id=?");
    stmt.bind(1, articles_reid_lib_id);
    return stmt.exec() > 0;
}

bool ArticlesReidDao::ClearArticlesReidLib(const std::string& articles_reid_lib_id) {
    // Delete all items in this things library
    SQLite::Statement stmt(Db(), "DELETE FROM t_articlesreid_feature WHERE articlesreid_lib_id=?");
    stmt.bind(1, articles_reid_lib_id);
    LOG_INFO("ClearArticlesReidLib {}", articles_reid_lib_id);
    return stmt.exec() > 0;
}

bool ArticlesReidDao::AddArticlesReid(const std::string& articles_reid_id,
                                      const std::string& articles_reid_lib_id,
                                      const std::string& articles_reid_name,
                                      const std::vector<float>& feature) {
    auto now_time = util::GetMilliseconds();

    SQLite::Statement stmt(
        Db(),
        "insert into t_articlesreid_feature ("
        "articlesreid_id, articlesreid_lib_id, picture_name, articlesreid_feature, modify_time, "
        "create_time"
        ") values (?,?,?,?,?,?)");
    stmt.bind(1, articles_reid_id);
    stmt.bind(2, articles_reid_lib_id);
    stmt.bind(3, articles_reid_name);
    stmt.bind(4, reinterpret_cast<const void*>(feature.data()), static_cast<int>(feature.size() * 4));
    stmt.bind(5, static_cast<int64_t>(now_time));
    stmt.bind(6, static_cast<int64_t>(now_time));
    LOG_INFO("Insert ArticlesReid {} To Db, Feature Size is:{}", articles_reid_id, feature.size());
    if (stmt.exec() <= 0) {
        return false;
    }
    return true;
}

bool ArticlesReidDao::UpdateArticlesReid(const std::string& articles_reid_id,
                                         const std::string& articles_reid_name) {
    auto now_time = util::GetMilliseconds();

    SQLite::Statement stmt(
        Db(), "update t_articlesreid_feature set picture_name=?,modify_time=? where articlesreid_id=?");
    stmt.bind(1, articles_reid_name);
    stmt.bind(2, static_cast<int64_t>(now_time));
    stmt.bind(3, articles_reid_id);

    return stmt.exec() > 0;
}

bool ArticlesReidDao::UpdateFeature(const std::string& articles_reid_id, const std::vector<float>& feature) {
    auto now_time = util::GetMilliseconds();

    SQLite::Statement stmt(
        Db(),
        "update t_articlesreid_feature set modify_time=?, articlesreid_feature=? where articlesreid_id=?");
    stmt.bind(1, static_cast<int64_t>(now_time));
    stmt.bind(2, reinterpret_cast<const void*>(feature.data()), static_cast<int>(feature.size() * 4));
    stmt.bind(3, articles_reid_id);

    if (stmt.exec() <= 0) {
        LOG_WARN("ArticlesReid {} Update Feature failed.", articles_reid_id);
        return false;
    }
    return true;
}

bool ArticlesReidDao::RemoveArticlesReid(const std::string& articles_reid_id) {
    SQLite::Statement stmt(Db(), "DELETE FROM t_articlesreid_feature WHERE articlesreid_id=?");
    stmt.bind(1, articles_reid_id);
    return stmt.exec() > 0;
}

std::vector<std::string> ArticlesReidDao::GetAllArticlesReidLibs() {
    std::vector<std::string> result;
    // Query statement
    std::string query_sql;
    query_sql.reserve(1024);
    query_sql.append("select articlesreid_lib_id from t_articlesreid_lib");

    SQLite::Statement query(Db(), query_sql);
    while (query.executeStep()) {
        std::string lib_id;

        RowFieldReader row(query);
        row.SetValue(lib_id);
        result.emplace_back(std::move(lib_id));
    }

    return result;
}

}  // namespace cosmo::db
