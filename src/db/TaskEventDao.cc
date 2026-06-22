// TaskEventDao — Task event query condition

#include "db/TaskEventDao.h"

#include <SQLiteCpp/SQLiteCpp.h>

#include "db/ConditionBuilder.h"
#include "db/RowFieldReader.h"
#include "util/DateTimeFormat.h"
#include "util/Log.h"
#include "util/UuidUtil.h"

namespace cosmo::db {

TaskEventDao::TaskEventDao(SQLite::Database& db) : DaoBase(db) {}

void TaskEventDao::CreateTable() {
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
            "libPersionName         text,"     // Person name (NOTE: historical column name typo)
            "libPersionNumber       text,"     // Person number (NOTE: historical column name typo)
            "libFacesID             text,"     // Belonging face library
            "video_flag             integer,"  // Has video flag
            "x                      integer,"  // Target position
            "y                      integer,"
            "width                  integer,"
            "height                 integer,"
            "eventFrame             integer,"  // Target frame index
            "propStr                text,"     // Target property string (e.g. plate number)
            "propColor              text,"     // Target color (e.g. vehicle color)
            "propRelatedColor       text,"     // Related target color (e.g. plate color)
            "propType               text,"     // Target type (e.g. vehicle type)
            "propDirection          text)");   // Target direction (e.g. vehicle direction)

    Db().exec(create_sql);

    auto all_columns = GetTableAllColumns(table_name_);

    if (!IsColumnExist(all_columns, "eventFrame")) {
        AddColumnToTable(table_name_, "eventFrame", ColumnType::INTEGER);
    }
    if (!IsColumnExist(all_columns, "libPersionName")) {
        AddColumnToTable(table_name_, "libPersionName", ColumnType::TEXT);
    }
    if (!IsColumnExist(all_columns, "libPersionNumber")) {
        AddColumnToTable(table_name_, "libPersionNumber", ColumnType::TEXT);
    }
    if (!IsColumnExist(all_columns, "libFacesID")) {
        AddColumnToTable(table_name_, "libFacesID", ColumnType::TEXT);
    }
    if (!IsColumnExist(all_columns, "propStr")) {
        AddColumnToTable(table_name_, "propStr", ColumnType::TEXT);
    }
    if (!IsColumnExist(all_columns, "propColor")) {
        AddColumnToTable(table_name_, "propColor", ColumnType::TEXT);
    }
    if (!IsColumnExist(all_columns, "propRelatedColor")) {
        AddColumnToTable(table_name_, "propRelatedColor", ColumnType::TEXT);
    }
    if (!IsColumnExist(all_columns, "propType")) {
        AddColumnToTable(table_name_, "propType", ColumnType::TEXT);
    }
    if (!IsColumnExist(all_columns, "propDirection")) {
        AddColumnToTable(table_name_, "propDirection", ColumnType::TEXT);
    }
}

// Task event record query (parameterized)
TaskEventsResult TaskEventDao::Query(const QueryTaskEventCondition& condition, int order) const {
    // Build parameterized conditions
    ConditionBuilder cb;
    cb.AddEqual("area_id", condition.area_id);
    cb.AddEqual("camera_name", condition.camera_name);
    cb.AddGreaterOrEqual("create_time", condition.time_begin);
    cb.AddLessThan("create_time", condition.time_end);
    cb.AddEqual("target_id", condition.track_id);
    cb.AddIn("algorithm_code", condition.algorithm_codes);
    cb.AddEqual("rec_id", condition.id);
    cb.AddIn("category", condition.categories);
    cb.AddEqual("libPersionName", condition.lib_person_name);
    cb.AddEqual("libPersionNumber", condition.lib_person_number);
    cb.AddEqual("libFacesID", condition.lib_faces_id);
    cb.AddEqual("propColor", condition.prop_color);
    cb.AddEqual("propRelatedColor", condition.prop_related_color);
    cb.AddEqual("propType", condition.prop_type);
    cb.AddEqual("propDirection", condition.prop_direction);
    if (condition.report_status != -1) {
        cb.AddEqual("isreported", condition.report_status);
    }

    // Base SELECT
    std::string base_sql;
    base_sql.reserve(1024);
    base_sql
        .append(
            " select "
            " rec_id, category,algorithm_code,create_time,camera_id,"
            " camera_outid,camera_name,area_id,area_name,disk_id,isreported,"
            " target_id,pic_url,fullpic_url, originepic_url, extraFiles, property, libPersionName, "
            "libPersionNumber, libFacesID, video_flag, x,"
            " y, width, height, eventFrame, propStr, propColor, propRelatedColor, propType, propDirection"
            " from ")
        .append(table_name_);

    auto where_clause = cb.BuildWhereClause();

    TaskEventsResult result{};
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
        TaskEventData event_data{};

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
        row.SetValue(event_data.lib_person_name);
        row.SetValue(event_data.lib_person_number);
        row.SetValue(event_data.lib_faces_id);
        row.SetValue(event_data.video_flag);
        row.SetValue(event_data.tar_x);
        row.SetValue(event_data.tar_y);
        row.SetValue(event_data.tar_width);
        row.SetValue(event_data.tar_height);
        row.SetValue(event_data.event_frame);
        row.SetValue(event_data.prop_str);
        row.SetValue(event_data.prop_color);
        row.SetValue(event_data.prop_related_color);
        row.SetValue(event_data.prop_type);
        row.SetValue(event_data.prop_direction);

        result.behavior_list.push_back(std::move(event_data));
    }

    return result;
}

bool TaskEventDao::Insert(const TaskEventData& data) {
    std::string insert_sql;
    insert_sql.reserve(1024);
    insert_sql.append(" insert into ")
        .append(table_name_)
        .append(
            " (rec_id, category, algorithm_code, create_time, camera_id, camera_outid, camera_name, area_id, "
            "area_name,"
            " disk_id, isreported, target_id, pic_url, fullpic_url, originepic_url, extraFiles, property, "
            "libPersionName, libPersionNumber, libFacesID, video_flag,"
            " x, y, width, height, eventFrame, propStr, propColor, propRelatedColor, propType, propDirection"
            " ) values ("
            "?,?,?,?,?,?,?,?,?,"
            "?,?,?,?,?,?,?,?,?,"
            "?,?,?,?,?,?,?,?,?,"
            "?,?,?,?)");

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
    stmt.bind(18, data.lib_person_name);
    stmt.bind(19, data.lib_person_number);
    stmt.bind(20, data.lib_faces_id);
    stmt.bind(21, data.video_flag);
    stmt.bind(22, data.tar_x);
    stmt.bind(23, data.tar_y);
    stmt.bind(24, data.tar_width);
    stmt.bind(25, data.tar_height);
    stmt.bind(26, static_cast<int64_t>(data.event_frame));
    stmt.bind(27, data.prop_str);
    stmt.bind(28, data.prop_color);
    stmt.bind(29, data.prop_related_color);
    stmt.bind(30, data.prop_type);
    stmt.bind(31, data.prop_direction);
    return stmt.exec() > 0;
}

bool TaskEventDao::UpdateRecordReportStatus(const std::string& rec_id, bool reported) {
    SQLite::Statement stmt(Db(), "UPDATE " + table_name_ + " SET isreported=? WHERE rec_id=?");
    stmt.bind(1, reported ? 1 : 0);
    stmt.bind(2, rec_id);
    return stmt.exec() > 0;
}

void TaskEventDao::RemoveItems(const std::vector<std::string>& list) {
    DeleteItems(table_name_, list);
}
}  // namespace cosmo::db
