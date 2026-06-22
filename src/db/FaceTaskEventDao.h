#pragma once

#include "db/DaoBase.h"

namespace SQLite {
class Database;
}

namespace cosmo::db {

// Task event query condition
struct QueryFaceTaskEventCondition {
    int64_t time_begin{0};
    int64_t time_end{0};
    std::string camera_name;
    std::string area_id;
    std::string track_id;        // UUID (min 128 bytes)
    std::string id;              // UUID (min 128 bytes)
    std::string algorithm_code;  // Algorithm code
    std::string category;        // Category
    bool is_export_total_count{true};
    int page_num{1};
    int page_size{10};
};

// Task event upload info
struct TaskFaceEventFileReport {
    std::string id;
    int report_status{-1};     // -1: no update, 0: unreported (default), 1: reported
    std::string pic_url;       // Detected picture URL
    std::string full_pic_url;  // Full picture URL
    std::string orig_pic_url;  // Original picture URL
    std::string video_url;     // Video URL
};

struct FaceTaskEventData {
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
};

// Task event query result
struct FaceTaskEventsResult {
    size_t total_count{0};
    std::string export_result_url;
    std::vector<FaceTaskEventData> behavior_list;
};

class FaceTaskEventDao : public DaoBase {
public:
    explicit FaceTaskEventDao(SQLite::Database& db);
    // Create table
    void CreateTable();
    // Query task events (order: 0=DESC, 1=ASC)
    [[nodiscard]] FaceTaskEventsResult Query(const QueryFaceTaskEventCondition& condition,
                                             int order = 0) const;
    // Insert task event record
    bool Insert(const FaceTaskEventData& data);
    // Remove oldest task events
    void RemoveItems(const std::vector<std::string>& list);

    bool UpdateVideoFlag(const std::string& record_id, int video_flag);

private:
    std::string table_name_{"t_faceEvent"};
};

}  // namespace cosmo::db
