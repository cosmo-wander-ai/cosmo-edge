// FaceTaskEventDao — Task event query condition

#include "db/FaceTaskEventDao.h"

#include <SQLiteCpp/SQLiteCpp.h>

#include "db/ConditionBuilder.h"
#include "db/RowFieldReader.h"
#include "util/Log.h"
#include "util/UuidUtil.h"

namespace cosmo::db {

FaceTaskEventDao::FaceTaskEventDao(SQLite::Database& db) : DaoBase(db) {}

void FaceTaskEventDao::CreateTable() {
    std::string create_sql;
    create_sql.reserve(2048);

    create_sql
        .append(
            "create table "
            "if not exists ")
        .append(table_name_)
        .append(
            " ("
            "rec_id                 text primary key,"
            "category               integer,"  // Behavior/Object category
            "algorithm_code         text,"     // Algorithm type
            "create_time            integer,"  // Alert timestamp
            "camera_id              text,"     // Camera ID
            "camera_outid           text,"     // External ID
            "camera_name            text,"     // Camera name
            "area_id                text,"     // Area ID
            "area_name              text,"     // Area name
            "disk_id                text,"     // Disk ID
            "isreported             integer,"  // Report flag
            "target_id              text,"     // Target/track ID
            "pic_url                text,"     // Alert picture URL
            "fullpic_url            text,"     // Full picture URL
            "originepic_url         text,"     // Original picture URL
            "extraFiles             text,"     // File list
            "property               text,"     // Property in JSON format
            "video_flag             integer,"  // Has video flag
            "x                      integer,"  // Target position
            "y                      integer,"
            "width                  integer,"
            "height                 integer,"
            "eventFrame             integer)");  // Target frame index

    Db().exec(create_sql);

    auto all_columns = GetTableAllColumns(table_name_);

    if (!IsColumnExist(all_columns, "eventFrame")) {
        AddColumnToTable(table_name_, "eventFrame", ColumnType::INTEGER);
    }
}

// Face task event record query (parameterized)
FaceTaskEventsResult FaceTaskEventDao::Query(const QueryFaceTaskEventCondition& condition, int order) const {
    // Build parameterized conditions
    ConditionBuilder cb;
    cb.AddEqual("area_id", condition.area_id);
    cb.AddEqual("camera_name", condition.camera_name);
    cb.AddGreaterOrEqual("create_time", condition.time_begin);
    cb.AddLessThan("create_time", condition.time_end);
    cb.AddEqual("target_id", condition.track_id);
    cb.AddEqual("algorithm_code", condition.algorithm_code);
    cb.AddEqual("rec_id", condition.id);
    cb.AddEqual("category", condition.category);

    // Base SELECT
    std::string base_sql;
    base_sql.reserve(1024);
    base_sql
        .append(
            " select "
            " rec_id, category,algorithm_code,create_time,camera_id,"
            " camera_outid,camera_name,area_id,area_name,disk_id,isreported,"
            " target_id,pic_url,fullpic_url, originepic_url, extraFiles, property, video_flag, x,"
            " y, width, height, eventFrame"
            " from ")
        .append(table_name_);

    auto where_clause = cb.BuildWhereClause();

    FaceTaskEventsResult result{};
    if (condition.is_export_total_count) {
        // Count total records
        std::string count_sql = "SELECT COUNT(*) FROM (" + base_sql + where_clause + ")";
        SQLite::Statement count_stmt(Db(), count_sql);
        cb.BindAll(count_stmt);
        if (count_stmt.executeStep()) {
            result.total_count = static_cast<size_t>(count_stmt.getColumn(0).getInt());
        }
    }

    // Build final query with ORDER BY and LIMIT
    std::string query_sql = base_sql + where_clause;
    query_sql.append(" order by create_time ");
    query_sql.append(order ? " asc " : " desc ");
    SetLimit(query_sql, condition.page_num, condition.page_size);

    SQLite::Statement query(Db(), query_sql);
    cb.BindAll(query);

    while (query.executeStep()) {
        FaceTaskEventData event_data{};

        RowFieldReader row(query);
        row.SetValue(event_data.id);
        row.SetValue(event_data.category);
        row.SetValue(event_data.algorithm_code);
        event_data.timestamp = query.getColumn(row.Step()).getInt64();
        row.SetValue(event_data.camera_id);
        row.SetValue(event_data.camera_out_id);
        row.SetValue(event_data.camera_name);
        row.SetValue(event_data.area_id);
        row.SetValue(event_data.area_name);
        row.SetValue(event_data.disk_id);
        row.SetValue(event_data.report_status);
        row.SetValue(event_data.track_id);
        row.SetValue(event_data.detected_pic_url);
        row.SetValue(event_data.full_pic_url);
        row.SetValue(event_data.orig_pic_url);
        row.SetValue(event_data.extra_files);
        row.SetValue(event_data.property);
        row.SetValue(event_data.video_flag);
        row.SetValue(event_data.tar_x);
        row.SetValue(event_data.tar_y);
        row.SetValue(event_data.tar_width);
        row.SetValue(event_data.tar_height);
        row.SetValue(event_data.event_frame);

        result.behavior_list.push_back(std::move(event_data));
    }

    return result;
}

bool FaceTaskEventDao::Insert(const FaceTaskEventData& data) {
    std::string insert_sql;
    insert_sql.reserve(1024);
    insert_sql.append(" insert into ")
        .append(table_name_)
        .append(
            " (rec_id, category, algorithm_code, create_time, camera_id, camera_outid, camera_name, area_id, "
            "area_name,"
            " disk_id, isreported, target_id, pic_url, fullpic_url, originepic_url, extraFiles, property, "
            "video_flag,"
            " x, y, width, height, eventFrame"
            " ) values ("
            "?,?,?,?,?,?,?,?,?,"
            "?,?,?,?,?,?,?,?,?,"
            "?,?,?,?,?)");

    SQLite::Statement stmt(Db(), insert_sql);
    stmt.bind(1, data.id);
    stmt.bind(2, data.category);
    stmt.bind(3, data.algorithm_code);
    stmt.bind(4, data.timestamp);
    stmt.bind(5, data.camera_id);
    stmt.bind(6, data.camera_out_id);
    stmt.bind(7, data.camera_name);
    stmt.bind(8, data.area_id);
    stmt.bind(9, data.area_name);
    stmt.bind(10, data.disk_id);
    stmt.bind(11, data.report_status);
    stmt.bind(12, data.track_id);
    stmt.bind(13, data.detected_pic_url);
    stmt.bind(14, data.full_pic_url);
    stmt.bind(15, data.orig_pic_url);
    stmt.bind(16, data.extra_files);
    stmt.bind(17, data.property);
    stmt.bind(18, data.video_flag);
    stmt.bind(19, data.tar_x);
    stmt.bind(20, data.tar_y);
    stmt.bind(21, data.tar_width);
    stmt.bind(22, data.tar_height);
    stmt.bind(23, static_cast<int64_t>(data.event_frame));
    return stmt.exec() > 0;
}

void FaceTaskEventDao::RemoveItems(const std::vector<std::string>& list) {
    DeleteItems(table_name_, list);
}

bool FaceTaskEventDao::UpdateVideoFlag(const std::string& record_id, int video_flag) {
    if (record_id.empty()) {
        return false;
    }

    SQLite::Statement stmt(Db(), "UPDATE " + table_name_ + " SET video_flag=? WHERE rec_id=?");
    stmt.bind(1, video_flag);
    stmt.bind(2, record_id);
    return stmt.exec() > 0;
}
}  // namespace cosmo::db
