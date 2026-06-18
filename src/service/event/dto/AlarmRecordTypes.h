/// @file AlarmRecordTypes.h
/// @brief Service-layer DTOs for alarm record queries — decouples the
///        IAlarmRecordService interface from db/ DAO implementation details.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cosmo::service {

// ─── Task Event Query Types ─────────────────────────────────────────

/// Query condition for alarm/task event records.
struct AlarmQueryCondition {
    int64_t timeBegin{0};
    int64_t timeEnd{0};
    std::string cameraName;
    std::string areaId;
    std::string trackId;
    std::string id;
    std::vector<std::string> algorithmCodes;
    std::vector<std::string> categorys;
    int reportStatus{-1};
    bool bExportTotalCount{true};
    int pageNum{1};
    int pageSize{10};

    std::string libPersionName;
    std::string libPersionNumber;
    std::string libFacesID;

    std::string propColor;
    std::string propRelatedColor;
    std::string propType;
    std::string propDirection;
};

/// Single alarm event record.
struct AlarmEventRecord {
    std::string id;
    std::string category;
    std::string algorithm_code;
    int64_t timestamp{0};
    std::string cameraId;
    std::string cameraOutId;
    std::string cameraName;
    std::string areaId;
    std::string areaName;
    std::string diskId;
    int reportStatus{0};
    std::string trackId;
    std::string detectedPicUrl;
    std::string fullPicUrl;
    std::string origPicUrl;
    int videoFlag{0};
    int tarX{0};
    int tarY{0};
    int tarWidth{0};
    int tarHeight{0};
    int64_t eventFrame{-1};
    std::string extraFiles;
    std::string property;

    std::string libPersionName;
    std::string libPersionNumber;
    std::string libFacesID;

    std::string propStr;
    std::string propColor;
    std::string propRelatedColor;
    std::string propType;
    std::string propDirection;
};

/// Paginated alarm event query result.
struct AlarmQueryResult {
    size_t totalCount{0};
    std::string exportResultUrl;
    std::vector<AlarmEventRecord> behaviorList;
};

// ─── Passenger Flow Query Types ─────────────────────────────────────

/// Time granularity for passenger flow queries.
enum class FlowTimeGranularity { None = 0, Hour = 1, Day = 2, Week = 3, Month = 4 };

/// Query condition for passenger flow statistics.
struct FlowQueryCondition {
    std::string cameraId;
    std::string algorithm_code;
    FlowTimeGranularity type{FlowTimeGranularity::None};
    uint64_t startHour{0};
    uint64_t endHour{0};
    int reported{-1};
};

/// Single passenger flow time point.
struct FlowTimePoint {
    uint64_t hour{0};
    std::string timeString;
    std::string cameraId;
    std::string algorithm_code;
    int enterNumber{0};
    int leaveNumber{0};
};

/// Passenger flow query result.
struct FlowQueryResult {
    size_t totalCount{0};
    std::vector<FlowTimePoint> numberList;
};

}  // namespace cosmo::service
