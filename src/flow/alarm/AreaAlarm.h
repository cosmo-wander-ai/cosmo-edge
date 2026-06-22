// AreaAlarm.h — Area alarm logic judgement class.
// Mainly used for: area intrusion, tripwire detection, absence, gathering,
// area people counting, people counting, etc.
//
// Implementation partitions (methods declared here, defined in separate .cc):
//   AreaAlarmParam.cc           — parameter parsing / modification / assignment
//   AreaAlarmPassFlow.cc        — count report + pass-flow statistics
//   AreaAlarmTargetAlarm.cc     — target alarm logic (intrusion / tripwire)
//   AreaAlarmTargetLimit.cc     — target-limit alarm logic (absence / crowd)
//   AreaAlarmWrongDirection.cc  — wrong-direction (retrograde) detection

#pragma once

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "flow/action/AlgActionBase.h"
#include "flow/task/TaskBaseParam.h"
#include "util/MsgDynamicElement.h"

namespace cosmo {
enum class AreaAlarmType {
    kTargetLimit = 0,         // Quantity limit category (absence/gathering) with
                              // areaLimitTargetCount/areaLimitTargetType/areaLimitDuration
    kTargetCountReport,       // Quantity reporting category (traffic/area counting) with
                              // areaCalcDuration/targetCalcType/CountBreakAreaType
    kTargetAlarm,             // Line crossing / tripwire with breakAreaType/breakLineType
    kDirection,               // Target direction area alarm (wrong way) with RetroDirect
    kInAreaDuration,          // Target enters area for a period (intrusion) with
                              // areaDuration/durationBreakAreaType
    kInArea,                  // Target enters area (human detection) with detectBreakAreaType
    kTargetLimit1,            // Quantity limit category for multiple areas action
    kTargetLimitMultTargets,  // Quantity limit category - multiple targets in area simultaneously
    kMax
};

enum class BreakAreaType {
    kIn = 0,         // Alarm inside area
    kOut,            // Alarm outside area
    kInto,           // From outside to inside
    kGoOut,          // From inside to outside
    kAreaMax,        // Area max boundary
    kLinePos = 101,  // Forward line crossing
    kLineNeg,        // Reverse line crossing
    kLineBothCalc,   // Bidirectional line crossing
    kLineMax,        // Line max boundary
    kMax
};

enum class BreakLineType {
    kUnOrdered = 0,  // Unordered line crossing
    kOrdered,        // Ordered line crossing
    kMax
};

enum class TargetCalcType {
    kIn = 0,           // Count targets inside area
    kOut,              // Count targets outside area
    kBreakLine,        // Count line crossing targets
    kBreakLineInArea,  // Count line crossing targets inside area
    kMax
};

constexpr bool ValidateAreaAlarmType(int value) noexcept {
    return value >= static_cast<int>(AreaAlarmType::kTargetLimit) &&
           value < static_cast<int>(AreaAlarmType::kMax);
}

constexpr bool ValidateLimitTargetCount(int value) noexcept {
    return value >= 0;
}

constexpr bool ValidateDuration(int value) noexcept {
    return value >= 0;
}

constexpr bool ValidateBreakAreaType(int value) noexcept {
    return value >= static_cast<int>(BreakAreaType::kIn) && value < static_cast<int>(BreakAreaType::kMax);
}

constexpr bool ValidateBreakLineType(int value) noexcept {
    return value >= static_cast<int>(BreakLineType::kUnOrdered) &&
           value < static_cast<int>(BreakLineType::kMax);
}

constexpr bool ValidateTargetCalcType(int value) noexcept {
    return value >= static_cast<int>(TargetCalcType::kIn) && value <= static_cast<int>(TargetCalcType::kMax);
}

struct MultTargetAreaLimitParam {
    std::string label;
    int area_limit_target_count{-1};
    AlgCompareType area_limit_target_type{AlgCompareType::AlgCompareTypeInvalid};
};

struct BAAreaAlarmParam {
    MsgInputAreaType input_area_type{MsgInputAreaType::Main};  // Input detection area
    int area_limit_target_count{0};   // Number of targets in area, used with area_limit_target_type
    bool target_count_change{false};  // Area target count change flag
    AlgCompareType area_limit_target_type{AlgCompareType::AlgCompareTypeLess};  // Target count limit type
    uint64_t area_duration_src{0};                             // Stay duration in area (parameter)
    uint64_t area_duration{0};                                 // Stay duration in area (ms)
    uint64_t area_duration_time_type{1};                       // Area stay duration coefficient
    BreakLineType break_line_type{BreakLineType::kUnOrdered};  // Line crossing type
    int tripping_wire_type{1};                                 // Line crossing count
    TargetCalcType target_calc_type{
        TargetCalcType::kIn};  // Target count type for area/people counting algorithms
    RetroDirect retro_direct{RetroDirect::RetroDirectNorth};  // Forward direction
    float retro_distance{0.05f};                              // Y-axis movement distance
    int area_sensitivity{0};            // Sensitivity (1-10) = 11 - sensitivity/10 (shift handover specific)
    float area_sensitivity_ratio{1.0};  // Sensitivity ratio = 11 - sensitivity/10
    std::vector<MultTargetAreaLimitParam> mult_target_limit_param;
};

class AreaAlarm : public AlgActionBase {
public:
    AreaAlarm(const std::string& taskId, ActionNode& action);
    ~AreaAlarm();

    // Modify parameters - modify based on existing parameters
    bool ModifyParam(const std::string& channelId, const std::string& taskId,
                     std::vector<MsgDynamicKeyValue>& params) override;
    // Set parameters - clear previous parameters and set new ones completely
    bool SetParam(const std::string& channelId, const std::string& taskId,
                  std::vector<MsgDynamicKeyValue>& params) override;
    // Set area - clear previous parameters and set new ones completely
    bool SetArea(const std::string& channelId, const std::string& taskId, std::vector<MsgTaskArea>& areas,
                 std::vector<MsgTaskArea>& shieldedAreas) override;

    void HandFrame(AlgDataPtr algData) override;

    // HandFrame helpers (DEBT-C08 B4)
    static bool HasLlmPrejudgedAlarm(const AlgDataPtr& algData);
    static bool IsSupportedDataType(AlgDataType dataType);
    static DataDetTrackClassifyPtr ResolveInputData(const AlgDataPtr& algData);

    [[nodiscard]] std::string GetFlowActionId() const {
        return action_info_.flowActionId;
    };

private:
    class TrackIdAreaDataUnit;
    class TrackIdAreaData;
    class TrackIdData;
    class AreaTargetLimit;
    class AreaTarget;
    // Traffic flow area targets
    class PassFlowAreaTargets;
    // Target history records
    class PassFlowTrackIdData;

    bool AnalysisKey(const MsgDynamicKeyValue& param, BAAreaAlarmParam& localParamEl);

    // Handle area target limits (e.g. max people for absence, min people for gathering)
    void HandAreaTargetLimit(AlgDataPtr algData, DataDetTrackClassifyPtr input);
    void AddAreaTargetHistory(DataDetTrackClassifyPtr input,
                              std::chrono::steady_clock::time_point& dataTimePoint);
    bool GetAreaTargetLimitResult(const AreaTarget& areaTarget);

    // Handle kTargetAlarm (intrusion/tripwire/garbage etc.)
    void HandAreaTargetAlarm(AlgDataPtr algData, DataDetTrackClassifyPtr input);
    void HandFriendTargetAlarm(AlgDataPtr algData, DataDetTrackClassifyPtr input);

    // Target area logic processing
    void AreaTargetAlarmHandArea(AlgDataPtr algData, TrackIdData& idData);

    // Check if tripwire area contains all areas and return area ID
    bool CheckBreakAllArea(const std::vector<std::string>& areas, const std::vector<MsgTaskArea>& localAreas,
                           std::string& outAreaId, std::string& outAreaName);

    // Target line crossing logic processing
    void AreaTargetAlarmHandLine(AlgDataPtr algData, TrackIdData& idData);

    // Check if target is in area (currently normal area only, used for area judgement)
    bool TargetInArea(const AiDetectRstEl& target, const std::string& areaId);

    // Target alarm condition judgement
    void AreaTargetAlarmHandAlarm(AlgDataPtr algData, TrackIdData& idData, MsgTaskArea& area,
                                  TrackIdAreaData& areaData);

    // Build a DataAlarmUnit from current track/area state (shared by detection and duration paths)
    DataAlarmUnit BuildAlarmUnit(const TrackIdData& idData, const MsgTaskArea& area) const;

    // Report area quantity
    void HandAreaTargetCountReport(AlgDataPtr algData, DataDetTrackClassifyPtr input);
    // Add person/object traffic flow history record
    void HandPassFlowAddHistory(AlgDataPtr algData, DataDetTrackClassifyPtr input);
    // Age person/object traffic flow history record
    void HandPassFlowOldHistory(bool bCalcPassflow = true);
    // Calculate person traffic flow count
    void HandPassFlowCalc();
    // Report person traffic flow
    void HandPassFlowReport(AlgDataPtr algData);
    // Calculate target line crossing count
    void PassFlowCount(PassFlowAreaTargets& areaData, const std::deque<AiDetectRstEl>& history,
                       const std::string& areaId);
    // Calculate area quantity for traffic/area counting algorithms
    void HandAreaTargetCount(AlgDataPtr algData, DataDetTrackClassifyPtr input);

    // Get max and min values
    void GetMaxMinValue(const std::deque<AiDetectRstEl>& history, const std::string& areaId, float& max,
                        float& min, int height);
    // Calculate target wrong direction in area
    void HandAreaTargetWrongDirection(AlgDataPtr algData, PassFlowAreaTargets& areaTargets,
                                      PassFlowTrackIdData& areaData);
    // Calculate wrong direction in area
    void HandAreaWrongDirection(AlgDataPtr algData);
    // Wrong direction handler
    void HandWrongDirection(AlgDataPtr algData, DataDetTrackClassifyPtr input);

    void FillAlarmData(AlgDataPtr algData, DataAlarmUnit& alarmUnit);
    void FillAlarmDataCombine(AlgDataPtr algData, DataAlarmUnit& alarmUnit);

    // Age tracking IDs in m_mapTrackIdStatus
    void TargetOldHistory();

    // Set multi-target judgement parameters
    void SetMultTargetAreaLimitTargetCount(const MsgDynamicKeyValue& param, const std::string& label);
    void SetMultTargetAreaLimitTargetType(const MsgDynamicKeyValue& param, const std::string& label);
    // Count multiple targets
    void MultTargetAddTarget(std::vector<MultTargetAreaLimitParam>& targetsInfo, const AiDetectRstEl& target);
    // Mult-target calculation result (single frame)
    bool MultTargetLogicResult(std::vector<MultTargetAreaLimitParam>& targetsInfo);

    ActionNode action_info_;
    bool is_alarm_needed_{false};
    DataAlarmPtr alarm_data_{nullptr};
    size_t frame_index_{0};
    bool has_track_{false};
    AreaAlarmType type_{AreaAlarmType::kTargetLimit};
    BreakAreaType break_area_type_{BreakAreaType::kIn};
    BAAreaAlarmParam params_;
    TaskBaseArea task_area_;
    bool has_area_{false};
    std::map<unsigned, TrackIdData> track_id_status_map_;
    std::map<std::string, PassFlowAreaTargets> pass_flow_areas_map_;
    std::map<std::string, AreaTargetLimit> area_target_status_map_;
    std::chrono::steady_clock::time_point report_time_point_;
};
using AreaAlarmPtr = std::shared_ptr<AreaAlarm>;
}  // namespace cosmo
