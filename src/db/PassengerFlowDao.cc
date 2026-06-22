// PassengerFlowDao — Flow record query condition

#include "db/PassengerFlowDao.h"

#include <SQLiteCpp/SQLiteCpp.h>

#include "db/ConditionBuilder.h"
#include "db/RowFieldReader.h"
#include "fmt/format.h"
#include "util/Log.h"
#include "util/UuidUtil.h"

namespace cosmo::db {

PassengerFlowDao::PassengerFlowDao(SQLite::Database& db) : DaoBase(db) {}

void PassengerFlowDao::CreateTable() {
    std::string create_sql;
    create_sql.reserve(1024);

    create_sql.append(
        "create table "
        "if not exists "
        "t_passengerflow "
        "("
        "camera_id  text not null,"
        "algorithm_code  text not null,"
        "hour       integer not null,"
        "enter_num  integer not null default 0,"
        "leave_num  integer not null default 0,"
        "isreported integer not null default 0,"
        "primary key(camera_id,algorithm_code,hour))");

    Db().exec(create_sql);
}

static ConditionBuilder BuildFlowCondition(const PassengerFlowCondition& condition) {
    ConditionBuilder cb;
    cb.AddEqual("b.camera_id", condition.camera_id);
    cb.AddEqual("b.algorithm_code", condition.algorithm_code);
    cb.AddGreaterOrEqual("b.hour", static_cast<int64_t>(condition.start_hour));
    cb.AddLessThan("b.hour", static_cast<int64_t>(condition.end_hour));
    if (condition.reported >= 0) {
        cb.AddEqual("b.isreported", condition.reported);
    }
    return cb;
}

static std::string& AppendTypeCondition(std::string& query_sql, PassengerFlowConditionType type) {
    switch (type) {
        case PassengerFlowConditionType::Hour:
            break;
        case PassengerFlowConditionType::Day:
        case PassengerFlowConditionType::Week:
            query_sql.append(" / 100 ");
            break;
        default:
            query_sql.append(" / 10000 ");
            break;
    }
    return query_sql;
}

PassengerFlowResult PassengerFlowDao::Query(const PassengerFlowCondition& condition, int max_size,
                                            int order) const {
    auto cb = BuildFlowCondition(condition);

    // Base query
    std::string base_sql =
        " select "
        " min(b.hour),sum(b.enter_num),sum(b.leave_num)"
        " from t_passengerflow b ";

    auto where_clause     = cb.BuildWhereClause();
    std::string group_sql = base_sql + where_clause + " group by b.hour ";
    AppendTypeCondition(group_sql, condition.type);

    PassengerFlowResult result{};

    // Count total records
    std::string count_sql = "SELECT COUNT(*) FROM (" + group_sql + ")";
    SQLite::Statement count_stmt(Db(), count_sql);
    cb.BindAll(count_stmt);
    if (count_stmt.executeStep()) {
        result.total_count = static_cast<size_t>(count_stmt.getColumn(0).getInt());
    }

    // Final query with ORDER BY and LIMIT
    group_sql.append(fmt::format(FMT_STRING(" order by b.hour {} "), order ? " asc " : " desc "));
    if (max_size) {
        group_sql.append(fmt::format(FMT_STRING(" limit {} "), max_size));
    }

    SQLite::Statement query(Db(), group_sql);
    cb.BindAll(query);

    while (query.executeStep()) {
        PassengerFlowTimePoint time_point{};

        RowFieldReader row(query);
        row.SetValue(time_point.hour);
        row.SetValue(time_point.enter_number);
        row.SetValue(time_point.leave_number);

        result.number_list.push_back(std::move(time_point));
    }
    return result;
}

PassengerFlowResult PassengerFlowDao::QueryOrigin(const PassengerFlowCondition& condition, int max_size,
                                                  int order) const {
    auto cb = BuildFlowCondition(condition);

    // Base query
    std::string base_sql =
        " select "
        " b.camera_id, b.algorithm_code, b.hour, b.enter_num, b.leave_num"
        " from t_passengerflow b ";

    auto where_clause    = cb.BuildWhereClause();
    std::string full_sql = base_sql + where_clause;
    AppendTypeCondition(full_sql, condition.type);

    PassengerFlowResult result{};

    // Count total records
    std::string count_sql = "SELECT COUNT(*) FROM (" + full_sql + ")";
    SQLite::Statement count_stmt(Db(), count_sql);
    cb.BindAll(count_stmt);
    if (count_stmt.executeStep()) {
        result.total_count = static_cast<size_t>(count_stmt.getColumn(0).getInt());
    }

    // Final query with ORDER BY and LIMIT
    full_sql.append(fmt::format(FMT_STRING(" order by b.hour {} "), order ? " asc " : " desc "));
    if (max_size) {
        full_sql.append(fmt::format(FMT_STRING(" limit {} "), max_size));
    }

    SQLite::Statement query(Db(), full_sql);
    cb.BindAll(query);

    while (query.executeStep()) {
        PassengerFlowTimePoint time_point{};

        RowFieldReader row(query);
        row.SetValue(time_point.camera_id);
        row.SetValue(time_point.algorithm_code);
        row.SetValue(time_point.hour);
        row.SetValue(time_point.enter_number);
        row.SetValue(time_point.leave_number);

        result.number_list.push_back(std::move(time_point));
    }

    return result;
}

bool PassengerFlowDao::AddNumber(const std::string& camera_id, const std::string& alg_id, uint64_t hour,
                                 int enter_num, int leave_num) {
    LOG_INFO("** {} | {} | {} | {} | {} **", camera_id, alg_id, hour, enter_num, leave_num);

    SQLite::Statement insert_stmt(Db(),
                                  "INSERT OR IGNORE INTO t_passengerflow "
                                  "(camera_id, algorithm_code, hour) VALUES (?,?,?)");
    insert_stmt.bind(1, camera_id);
    insert_stmt.bind(2, alg_id);
    insert_stmt.bind(3, static_cast<int64_t>(hour));
    insert_stmt.exec();

    SQLite::Statement update_stmt(Db(),
                                  "UPDATE t_passengerflow SET "
                                  "enter_num=enter_num+?, leave_num=leave_num+?, isreported=0 "
                                  "WHERE camera_id=? AND algorithm_code=? AND hour=?");
    update_stmt.bind(1, enter_num);
    update_stmt.bind(2, leave_num);
    update_stmt.bind(3, camera_id);
    update_stmt.bind(4, alg_id);
    update_stmt.bind(5, static_cast<int64_t>(hour));
    return update_stmt.exec() > 0;
}

bool PassengerFlowDao::ResetDateNumber(const std::string& camera_id, const std::string& alg_id,
                                       uint64_t date) {
    SQLite::Statement stmt(Db(),
                           "UPDATE t_passengerflow SET "
                           "enter_num=0, leave_num=0, isreported=0 "
                           "WHERE camera_id=? AND algorithm_code=? AND hour>=? AND hour<?");
    stmt.bind(1, camera_id);
    stmt.bind(2, alg_id);
    stmt.bind(3, static_cast<int64_t>(date * 100));
    stmt.bind(4, static_cast<int64_t>((date + 1) * 100));
    return stmt.exec() > 0;
}

bool PassengerFlowDao::Reported(const std::string& camera_id, const std::string& alg_id, uint64_t hour) {
    SQLite::Statement stmt(Db(),
                           "UPDATE t_passengerflow SET isreported=1 "
                           "WHERE camera_id=? AND algorithm_code=? AND hour=?");
    stmt.bind(1, camera_id);
    stmt.bind(2, alg_id);
    stmt.bind(3, static_cast<int64_t>(hour));
    return stmt.exec() > 0;
}

}  // namespace cosmo::db
