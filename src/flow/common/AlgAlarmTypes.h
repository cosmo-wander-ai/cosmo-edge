// Algorithm Alarm Types definitions

#pragma once

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "flow/common/AlgCommonType.h"
#include "infer/AiCommon.h"
#include "media/VideoFrame.h"
#include "util/Rect.h"
#include "util/dto/ClientMsgEvent.h"

namespace cosmo {

enum class AlarmDataType {
    AlarmDataTypeTrack,               // Alarm with tracking and trackId
    AlarmDataTypeNoneTrackSensitity,  // Sensitivity alarm without tracking
    AlarmDataTypeTargetCount,         // Statistics report category
};

struct RecordParam {
    std::string channelId;
    std::string taskId;
    int targetId{-1};
    std::string recordId;
    std::string url;
    std::string jsonUrl;
    std::string jsonPath;
    std::string overviewUrl;
    int64_t streamIndex{-1};
    int64_t frameSeq{-1};
    int64_t frameTimestamp{-1};
    int64_t startFrameSeq{-1};
    int64_t startframeTimestamp{-1};
    RetroDirect retroDirect{RetroDirect::RetroDirectNone};  // Invalid direction
};

struct DataTargetFilter {
    bool bFilter{false};                 // Will be true if any other bool filter is true
    bool bSizeFilter{false};             // Size filter
    bool bSideFilter{false};             // Edge filter
    bool bConfidenceFilter{false};       // Confidence filter
    bool bAlarmLimitCountFilter{false};  // Alarm count filter
    bool bAlarmDurationFilter{false};    // Alarm interval filter
    std::vector<std::string> areaId;     // Inner area list including both normal and shielded areas
};

struct DataAlarmTargetConfidence {
    util::Box box;
    std::vector<util::Box> friends;
    TargetPosition targetPos{TargetPosition::kBottom};
};

struct DataAlarmPassFlow {
    int enterNumber{0};    // Cumulative entered people per hour, overwrites previous data if repeatedly sent
    int leaveNumber{0};    // Cumulative departed people per hour, overwrites previous data if repeatedly sent
    int enterOrgNum{0};    // Entered people during interval, not cumulatively added
    int leaveOrgNum{0};    // Departed people during interval, not cumulatively added
    int enterTotalNum{0};  // Total cumulative entered people
    int leaveTotalNum{0};  // Total cumulative departed people
    std::string time;      // Reporting time accurate to hour
    std::string timeSec;   // Reporting time accurate to second. E.g., 2020-01-01 10:12:15
};

struct AlgTaskDataFaceLogicAreaInfo {
    bool bHaveIntoArea{false};     // Has data entering area
    bool bHaveOutArea{false};      // Has data exiting area
    VideoFramePtr intoAreaFrame;   // Frame entering area (effective when bHaveIntoArea is true)
    AiDetectRstEl intoAreaTarget;  // Box when entering area (effective when bHaveIntoArea is true)
    VideoFramePtr outAreaFrame;    // Frame exiting area (effective when bHaveIntoArea is true)
    AiDetectRstEl outAreaTarget;   // Box when exiting area (effective when bHaveIntoArea is true)
    std::string areaId;
    std::string areaName;
};

struct DataAlarmUnit {
    //    OnEventsPropertyType type{OnEventsPropertyType::None};      // property type
    std::string flowActionId;
    std::string areaId;
    std::string areaName;
    std::string assoAreaId;    // Associated area
    std::string assoAreaName;  // Associated area name
    std::string ocrString;     // OCR recognition result
    VideoFramePtr ocrImage;    // OCR image
    bool statusChange{false};  // Area people count changed
    int trackId{-1};
    std::string strTrackId;
    util::Box box;
    std::vector<util::Box> friends;
    bool haveRelated{false};
    util::Box relatedBox;
    std::vector<AiConfidence> confidence;  // Classification confidence
    std::vector<AiAttribute> attrRsts;     // Classification attribute results
    AiFeature feature;                     // Feature values for face comparison
    std::vector<util::Box> boxs;           // Alarms without tracking (multiple detections in one area)
    std::vector<AiDetectBestEl> bestInfos;
    std::vector<DataAlarmTargetConfidence> targetHistory;  // Target confidence during sensitivity calculation
    RetroDirect retroDirect{RetroDirect::RetroDirectNone};  // No direction
    DataAlarmPassFlow passFlowData;  // Effective for OnEventsPropertyTypePeople and OnEventsPropertyTypeCar

    OnEventsReportType reportType{OnEventsReportType::Trigger};
    OnEventsFilterStatus filterStatus{OnEventsFilterStatus::None};  // Filter status
    AiDetectMatchHighScoreInfo matchInfo;   // Highest score match info during Reid algorithm comparison
    AlgTaskDataFaceLogicAreaInfo areaInfo;  // Effective only when OnEventsReportTypeTrigger
    VideoFramePtr baseFrame;                // Base frame for camera movement
    bool bLogicResult{false};
    /// Graphic-text judgment is already completed by upstream (e.g. Qwen3VL node), event reporting side does
    /// not do a second LLM audit
    bool bLlmPrejudged{false};
};

struct DataAlarm {
    AlarmDataType type{AlarmDataType::AlarmDataTypeTrack};
    std::string flowActionId;
    unsigned int multiAlarms{0};       // Multiple alarm components overlaid
    std::deque<DataAlarmUnit> alarms;  // AlarmDataTypeTrack
};
using DataAlarmPtr = std::shared_ptr<DataAlarm>;

struct DataFilter {
    std::vector<DataTargetFilter> targetsFilter;
};
using DataFilterPtr = std::shared_ptr<DataFilter>;

}  // namespace cosmo
