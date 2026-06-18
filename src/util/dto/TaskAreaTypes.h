// Task area, target, and config types — MsgTaskArea, MsgTarget, MsgTaskConfig, etc.

#pragma once

#include "util/MsgBaseTypes.h"

namespace cosmo {

struct MsgTaskFaceSetConfig {
    std::string faceSetToken;
    float faceSetThreshold{0.0};
    std::string nodeN;
    float faceSetVersion{0.0};
    friend void to_json(nlohmann::json& j, const MsgTaskFaceSetConfig& v);
    friend void from_json(const nlohmann::json& j, MsgTaskFaceSetConfig& v);
};

enum class DirectionType {
    DirectionTypeOneWay = 0,  // One-way
    DirectionTypeTwoWay = 1,  // Two-way
};

enum class RetroDirect {
    RetroDirectNone = -1  // Invalid parameter
    ,
    RetroDirectNorth = 0  // North is forward
    ,
    RetroDirectSouth  // South is forward

    ,
    RetroDirectMax  //
};
constexpr bool ValidateRetroDirect(int value) noexcept {
    return value > static_cast<int>(RetroDirect::RetroDirectNone) &&
           value < static_cast<int>(RetroDirect::RetroDirectMax);
}
struct MsgTaskArea {
    std::string areaId;
    std::string name;
    bool bHaveAssoArea{false};
    std::vector<MsgPoint> points;
    MsgRect pointBox;  // Convert point to box
    std::vector<MsgPoint> linePoints;
    std::vector<MsgTaskArea> associatedAreas;
    std::vector<MsgDynamicKeyValue> params;  // Tripwire parameters
    // std::string derectionType{"-1"};   // 0: One-way line, 1: Two-way line (DirectionTypeOneWay)
    std::string retroDirect{"-1"};                           // 0: North, 1: South, Direction line
    RetroDirect iretroDirect{RetroDirect::RetroDirectNone};  // No direction
    DirectionType iderectionType{DirectionType::DirectionTypeOneWay};
};

// Conditional: retroDirect only serialized when iretroDirect is valid
void to_json(nlohmann::json& j, const MsgTaskArea& a);
void from_json(const nlohmann::json& j, MsgTaskArea& a);

struct MsgRunTime {
    std::string timeBegin;  // Start time (e.g. 00:00:00)
    std::string timeEnd;    // End time (e.g. 20:00:00)
};
// Mandatory fields
void to_json(nlohmann::json& j, const MsgRunTime& r);
void from_json(const nlohmann::json& j, MsgRunTime& r);

struct MsgTaskRunTime {
    int weekDay{0};  // 1-6: Mon-Sat, 0: Sun
    std::vector<MsgRunTime> runTime;
};

struct MsgAiConfidence {
    std::string label;
    float confidence{0.0};
    friend void to_json(nlohmann::json& j, const MsgAiConfidence& v);
    friend void from_json(const nlohmann::json& j, MsgAiConfidence& v);
};

struct MsgAiAttribute {
    std::string category;  // Category
    std::string label;     // Label
    friend void to_json(nlohmann::json& j, const MsgAiAttribute& v);
    friend void from_json(const nlohmann::json& j, MsgAiAttribute& v);
};

struct MsgMatchInfo {
    int64_t setPicCount{-1};  // Number of pictures in comparison library. setPicCount=0: No pictures, no
                              // comparison (no alarm). setPicCount>0: participated in comparison
    std::string matchId;      // Highest score target ID (highest similarity even if threshold not met)
    float matchDegree{-1.0};  // Highest similarity score
    std::string groupId;      // Highest score match library ID
    std::string groupName;    // Highest score match library name
    bool matched{false};      // Matched

    friend void to_json(nlohmann::json& j, const MsgMatchInfo& v);
    friend void from_json(const nlohmann::json& j, MsgMatchInfo& v);
};

struct MsgTarget {
    int trackId{-1};
    bool bFilter;
    bool bLogicResult{false};
    std::string filterDesc;
    std::string trackStatus;
    std::string motionStatus;
    std::string shapeChangeStatus;
    MsgRect box;
    MsgRectReal aiBox;
    float hwRatio{0.0};
    float hwRatioVariation{0.0};
    std::vector<MsgAiConfidence> confidence;
    std::vector<MsgAiAttribute> attrs;
    std::vector<std::string> areas;
    std::vector<std::string> shiledAreas;
    std::vector<int> groupEls;
    bool bHaveMatchInfo{false};
    MsgMatchInfo matchInfo;
};

// Conditional serialization: matchInfo/groupEls only when their flags/data exist
void to_json(nlohmann::json& j, const MsgTarget& t);
void from_json(const nlohmann::json& j, MsgTarget& t);

struct MsgAlarmVideoOverviewFrame {
    int64_t index;
    int color;
    std::vector<MsgRect> rects;
    friend void to_json(nlohmann::json& j, const MsgAlarmVideoOverviewFrame& v);
    friend void from_json(const nlohmann::json& j, MsgAlarmVideoOverviewFrame& v);
};
struct MsgAlarmVideoOverviewInfo {
    std::string algorithmCode;
    MsgTaskArea area;
    std::vector<MsgAlarmVideoOverviewFrame> targets;
    friend void to_json(nlohmann::json& j, const MsgAlarmVideoOverviewInfo& v);
    friend void from_json(const nlohmann::json& j, MsgAlarmVideoOverviewInfo& v);
};

struct MsgTaskConfig {
    std::vector<MsgTaskFaceSetConfig> facesetConfig;
    std::vector<MsgTaskArea> areas;
    std::vector<MsgTaskArea> shieldedAreas;
    std::vector<MsgDynamicKeyValue> params;
    //    std::vector<MsgTaskRunTime> runTimes;
    friend void to_json(nlohmann::json& j, const MsgTaskConfig& v);
    friend void from_json(const nlohmann::json& j, MsgTaskConfig& v);
};

/// Check if a MsgTaskConfig has no meaningful content.
inline bool IsTaskConfigEmpty(const MsgTaskConfig& config) noexcept {
    return config.facesetConfig.empty() && config.areas.empty() && config.shieldedAreas.empty() &&
           config.params.empty();
}

}  // namespace cosmo
