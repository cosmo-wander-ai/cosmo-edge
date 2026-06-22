// PersonDao — Person and face library data access object.

#include "db/PersonDao.h"

#include <SQLiteCpp/SQLiteCpp.h>

#include <chrono>

#include "db/ConditionBuilder.h"
#include "db/RowFieldReader.h"
#include "util/DurationLogger.h"
#include "util/Log.h"
#include "util/StringUtil.h"
#include "util/TimeUtil.h"

namespace cosmo::db {
// Legacy compatibility: timestamps recorded as seconds before 2021-01-08.
// Max second-based timestamp is a 10-digit number.
constexpr int64_t kMaxSecond = 9999999999;

PersonDao::PersonDao(SQLite::Database& db) : DaoBase(db) {}

void PersonDao::CreateTable() {
    std::string create_sql;
    create_sql.reserve(1024);

    // Face library table
    create_sql.append(
        "create table "
        "if not exists "
        "t_faceset "
        "("
        "faceset_token      TEXT  PRIMARY KEY,"
        "alias              TEXT,"
        "description        TEXT,"
        "threshold          TEXT,"
        "capacity           TEXT,"
        "modify_desc        TEXT,"
        "modify_time        INTEGER,"
        "create_time        INTEGER)");
    Db().exec(create_sql);
    create_sql.clear();

    // Person table
    create_sql.append(
        "create table "
        "if not exists "
        "t_person("
        "person_id       TEXT  PRIMARY KEY,"
        "name            TEXT,"
        "sex             INTEGER,"
        "birthday        TEXT,"
        "telephone       TEXT,"
        "user_data       TEXT,"
        "person_type     INTEGER,"
        "ic_card         TEXT,"
        "id_card         TEXT,"
        "ref_count       INTEGER,"
        "modify_time     INTEGER,"
        "create_time     INTEGER)");
    Db().exec(create_sql);
    create_sql.clear();

    // Person-FaceLib relation table
    create_sql.append(
        "create table "
        "if not exists "
        "t_faceset_person_relation("
        "faceset_token   TEXT,"
        "person_id      TEXT,"
        "modify_time    INTEGER,"
        "create_time    INTEGER)");
    Db().exec(create_sql);
    create_sql.clear();

    // Face table
    create_sql.append(
        "create table "
        "if not exists "
        "t_face("
        "face_token     TEXT PRIMARY KEY,"
        "person_id      TEXT,"
        "face_feature   BLOB,"
        "img_url        TEXT,"
        "modify_time    INTEGER,"
        "create_time    INTEGER)");
    Db().exec(create_sql);
    create_sql.clear();
}

FacePersonQueryResult PersonDao::QueryPersons(const FacePersonQueryCondition& condition) const {
    // Build parameterized conditions
    ConditionBuilder cb;
    cb.AddIn("fs.faceset_token", condition.face_lib_id_list);
    cb.AddEqual("p.person_id", condition.person_id);
    cb.AddLike("p.name", condition.person_name);
    cb.AddEqual("p.ic_card", condition.serial_number);

    auto where_clause = cb.BuildWhereClause();

    // Count query
    std::string count_sql =
        "select p.person_id from t_person p"
        " join t_faceset_person_relation fsp on fsp.person_id=p.person_id"
        " join t_faceset fs on fsp.faceset_token=fs.faceset_token";
    count_sql += where_clause;
    count_sql += " group by p.person_id";

    std::string query_sql;
    query_sql.reserve(1024);
    query_sql.append(
        " select"
        " p.person_id,p.name,p.ic_card,p.modify_time,p.create_time,"
        " group_concat(distinct fs.faceset_token),"
        " group_concat(distinct fs.alias) as faceset_name,"
        " group_concat(distinct fa.face_token)"
        " from t_person p"
        " join t_faceset_person_relation fsp on fsp.person_id=p.person_id"
        " join t_faceset fs on fsp.faceset_token=fs.faceset_token"
        " join t_face fa on fa.person_id=p.person_id");
    query_sql += where_clause;
    query_sql += " group by p.person_id";

    FacePersonQueryResult result{};

    util::DurationLogger loggerCount("person count");
    // Count with parameterized query
    std::string count_wrap = "SELECT COUNT(*) FROM (" + count_sql + ")";
    SQLite::Statement count_stmt(Db(), count_wrap);
    cb.BindAll(count_stmt);
    if (count_stmt.executeStep()) {
        result.total_count = static_cast<size_t>(count_stmt.getColumn(0).getInt());
    }
    loggerCount.Print();

    SetLimit(query_sql, condition.page_num, condition.page_size);
    util::DurationLogger loggerQuery("person query");
    SQLite::Statement query(Db(), query_sql);
    cb.BindAll(query);
    loggerQuery.Print();
    while (query.executeStep()) {
        FacePersonRecord person{};

        RowFieldReader row(query);
        row.SetValue(person.id);
        row.SetValue(person.name);
        row.SetValue(person.serial_number);
        person.update_timestamp = query.getColumn(row.Step()).getInt64();
        person.create_timestamp = query.getColumn(row.Step()).getInt64();
        if (person.create_timestamp == 0) {
            person.create_timestamp = person.update_timestamp;
        }

        // Legacy compatibility: convert seconds to milliseconds
        if (person.update_timestamp <= kMaxSecond) {
            person.update_timestamp *= 1000;
        }
        if (person.create_timestamp <= kMaxSecond) {
            person.create_timestamp *= 1000;
        }

        // getString() returns a temporary std::string; Split() returns string_views.
        // Must keep the source strings alive while string_views are in use.
        auto face_lib_tokens = query.getColumn(row.Step()).getString();
        auto face_lib_names  = query.getColumn(row.Step()).getString();
        auto v_id            = util::Split(face_lib_tokens, ",");
        auto v_name          = util::Split(face_lib_names, ",");
        if (v_id.size() != v_name.size()) {
            LOG_ERRO("{}", "faceLib size error.");
        }
        for (size_t i = 0; i < v_id.size() && i < v_name.size(); ++i) {
            FaceLibRef ref{};
            ref.id   = v_id[i];
            ref.name = v_name[i];
            person.face_lib_list.push_back(std::move(ref));
        }

        auto face_tokens = query.getColumn(row.Step()).getString();
        for (auto& sv : util::Split(face_tokens, ",")) {
            if (!sv.empty()) {
                FacePersonPicture pic{};
                pic.id = sv;
                person.picture_list.push_back(std::move(pic));
            }
        }

        result.person_list.push_back(std::move(person));
    }

    return result;
}

FaceLibQueryResult PersonDao::QueryFaceLib(const FaceLibQueryCondition& condition) const {
    // Build parameterized conditions
    ConditionBuilder cb;
    cb.AddLike("faceset_name", condition.face_lib_name);
    cb.AddEqual("fs.faceset_token", condition.face_lib_id);

    auto where_clause = cb.BuildWhereClause();

    // Count query
    std::string count_sql = "select fs.faceset_token, fs.alias as faceset_name from t_faceset fs";
    count_sql += where_clause;

    std::string query_sql;
    query_sql.reserve(1024);
    query_sql.append(
        " select"
        " fs.faceset_token,fs.alias as faceset_name,"
        " fs.threshold,fs.capacity,fs.modify_time,fs.create_time,"
        " count(distinct fsp.person_id) as person_count,"
        " count(distinct f.face_token) as face_count"
        " from t_faceset fs"
        " left join t_faceset_person_relation fsp on fsp.faceset_token=fs.faceset_token"
        " left join t_face f on f.person_id=fsp.person_id");
    query_sql += where_clause;
    query_sql += " group by fs.faceset_token order by fs.create_time desc";

    FaceLibQueryResult result{};
    util::DurationLogger loggerCount("faceLib count");
    // Count with parameterized query
    std::string count_wrap = "SELECT COUNT(*) FROM (" + count_sql + ")";
    SQLite::Statement count_stmt(Db(), count_wrap);
    cb.BindAll(count_stmt);
    if (count_stmt.executeStep()) {
        result.face_lib_count = static_cast<size_t>(count_stmt.getColumn(0).getInt());
    }
    loggerCount.Print();

    SetLimit(query_sql, condition.page_num, condition.page_size);
    util::DurationLogger loggerQuery("faceLib query");
    SQLite::Statement query(Db(), query_sql);
    cb.BindAll(query);
    loggerQuery.Print();
    while (query.executeStep()) {
        FaceLibRecord face_lib{};

        RowFieldReader row(query);
        row.SetValue(face_lib.id);
        row.SetValue(face_lib.name);
        face_lib.threshold        = query.getColumn(row.Step()).getDouble();
        face_lib.max_face_number  = query.getColumn(row.Step()).getInt();
        face_lib.update_timestamp = query.getColumn(row.Step()).getInt64();
        face_lib.create_timestamp = query.getColumn(row.Step()).getInt64();
        row.SetValue(face_lib.person_number);
        row.SetValue(face_lib.face_number);

        // Legacy compatibility: convert seconds to milliseconds
        if (face_lib.update_timestamp <= kMaxSecond) {
            face_lib.update_timestamp *= 1000;
        }
        if (face_lib.create_timestamp <= kMaxSecond) {
            face_lib.create_timestamp *= 1000;
        }

        result.face_lib_list.push_back(std::move(face_lib));
    }

    return result;
}

std::vector<float> PersonDao::QueryFaceFeature(const std::string& face_id) const {
    std::vector<float> result;

    SQLite::Statement query(Db(), "SELECT face_feature FROM t_face WHERE face_token=?");
    query.bind(1, face_id);
    if (query.executeStep()) {
        auto col         = query.getColumn(0);
        auto feature_str = reinterpret_cast<const float*>(col.getBlob());
        int length       = col.getBytes();
        result.assign(feature_str, feature_str + length / 4);
    }

    return result;
}

bool PersonDao::AddFaceLib(const LibInfo& data) {
    auto now_time = util::GetMilliseconds();

    SQLite::Statement stmt(
        Db(),
        "insert into t_faceset ("
        "faceset_token, alias, description, capacity, modify_desc, threshold, modify_time, create_time"
        ") values (?,?,?,?,?,?,?,?)");
    stmt.bind(1, data.id);
    stmt.bind(2, data.name);
    stmt.bind(3, "");
    stmt.bind(4, std::to_string(data.max_capacity));
    stmt.bind(5, "");
    stmt.bind(6, std::to_string(data.threshold));
    stmt.bind(7, static_cast<int64_t>(now_time));
    stmt.bind(8, static_cast<int64_t>(now_time));

    return stmt.exec() > 0;
}

// Person query operations — moved to PersonDaoQuery.cc

}  // namespace cosmo::db
