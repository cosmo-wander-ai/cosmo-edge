#pragma once

#include "db/DaoBase.h"

namespace SQLite {
class Database;
}

namespace cosmo::db {

// Task event query condition
struct QueryTaskEventCondition {
    int64_t time_begin{0};
    int64_t time_end{0};
    std::string camera_name;
    std::string area_id;
    std::string track_id;                      // UUID (min 128 bytes)
    std::string id;                            // UUID (min 128 bytes)
    std::vector<std::string> algorithm_codes;  // Algorithm code
    std::vector<std::string> categories;       // Category
    int report_status{-1};
    bool is_export_total_count{true};
    int page_num{1};
    int page_size{10};

    std::string lib_person_name;    // Person name
    std::string lib_person_number;  // Person number
    std::string lib_faces_id;       // Belonging face library

    std::string prop_color;          // Target color (e.g. vehicle color)
    std::string prop_related_color;  // Related target color (e.g. plate color)
    std::string prop_type;           // Target type (e.g. vehicle type)
    std::string prop_direction;      // Target direction (e.g. vehicle direction)
};

// Task event upload info
struct TaskEventFileReport {
    std::string id;
    int report_status{-1};     // -1: no update, 0: unreported (default), 1: reported
    std::string pic_url;       // Detected picture URL
    std::string full_pic_url;  // Full picture URL
    std::string orig_pic_url;  // Original picture URL
    std::string video_url;     // Video URL
};

struct TaskEventData {
    std::string id;
    std::string category;
    std::string algorithm_code;
    int64_t timestamp{0};
    std::string camera_id;
    std::string camera_out_id;
    std::string camera_name;
    std::string area_id;
    std::string area_name;
    std::string disk_id;
    int report_status{0};
    std::string track_id;  // Track ID for related face recognition
    std::string detected_pic_url;
    std::string full_pic_url;
    std::string orig_pic_url;
    int video_flag{0};
    int tar_x{0};
    int tar_y{0};
    int tar_width{0};
    int tar_height{0};
    int64_t event_frame{-1};  // Event frame sequence in video
    std::string extra_files;  // File list
    std::string property;     // Property in JSON format

    std::string lib_person_name;    // Person name
    std::string lib_person_number;  // Person number
    std::string lib_faces_id;       // Belonging face library

    std::string prop_str;            // Target property string (e.g. plate number)
    std::string prop_color;          // Target color (e.g. vehicle color)
    std::string prop_related_color;  // Related target color (e.g. plate color)
    std::string prop_type;           // Target type (e.g. vehicle type)
    std::string prop_direction;      // Target direction (e.g. vehicle direction)
};

// Task event query result
struct TaskEventsResult {
    size_t total_count{0};
    std::string export_result_url;
    std::vector<TaskEventData> behavior_list;
};

class TaskEventDao : public DaoBase {
public:
    explicit TaskEventDao(SQLite::Database& db);
    // Create table
    void CreateTable();
    // Query task events (order: 0=DESC, 1=ASC)
    [[nodiscard]] TaskEventsResult Query(const QueryTaskEventCondition& condition, int order = 0) const;
    // Insert task event record
    bool Insert(const TaskEventData& data);
    // Remove oldest task events
    void RemoveItems(const std::vector<std::string>& list);

    bool UpdateRecordReportStatus(const std::string& rec_id, bool reported);

private:
    std::string table_name_{"t_commonEvent"};
};

}  // namespace cosmo::db
