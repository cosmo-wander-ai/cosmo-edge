#pragma once

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "flow/action/AlgActionBase.h"
#include "flow/common/AlgDataQueueDistributor.h"
#include "flow/common/AlgDataRecord.h"
#include "flow/overview/OverviewRecordSensitivityRst.h"
#include "flow/task/TaskBaseParam.h"
#include "util/MsgDynamicElement.h"

namespace cosmo {
enum class SensitivityType {
    kDuration = 0  // Sensitivity over a period of time
    ,
    kFixCount  // Sensitivity of a fixed count

    ,
    kMax  //
};

struct BASensitivityParam {
    long long int durationMs{0};     // Detection time in ms
    long long int durationMsMax{0};  // Detection time upper limit
    int64_t trackDetCount{-1};       // Tracked target detection count
    int sensitivity{0};              // 1-10 sensitivity
    float sensitivityRatio{1.0f};    // Sensitivity ratio
    bool filterToDenominator{true};  // Add filtered to denominator
    size_t senHitCount{1};           // Hit count
    size_t senTotalCount{10};        // Total count for sensitivity calculation
    SensitivityType sensitivityType{SensitivityType::kDuration};
};

class Sensitivity : public AlgActionBase {
public:
    Sensitivity(const std::string& taskId, ActionNode& action);
    ~Sensitivity();

    // Modify parameters based on existing ones
    bool ModifyParam(const std::string& channelId, const std::string& taskId,
                     std::vector<MsgDynamicKeyValue>& params) override;

    // Set parameters - clear previous ones and set fully new ones
    bool SetParam(const std::string& channelId, const std::string& taskId,
                  std::vector<MsgDynamicKeyValue>& params) override;

    // Set areas - clear previous ones and set fully new ones
    bool SetArea(const std::string& channelId, const std::string& taskId, std::vector<MsgTaskArea>& areas,
                 std::vector<MsgTaskArea>& shieldedAreas) override;

    // Get overlay info
    MsgOverviewMem GetOverviewInfo(const std::string& channelId, const std::string& taskId,
                                   int64_t streamIndex = -1, int64_t from = -1, int64_t to = -1) override;

private:
    class TrackIdData;
    class AreaTargetData;
    class AreaIdDataEl;
    class TargetSensitivityDetCount;

    void HandFrame(AlgDataPtr algData) override;

    bool AnalysisKey(MsgDynamicKeyValue& param, BASensitivityParam& localParamEl);

    void AddHistory(DataDetTrackClassifyPtr input,
                    const std::chrono::steady_clock::time_point& dataTimePoint);
    void OldHistory();
    void CalcSensitity(AlgDataPtr algData);
    void CalcSensitityWithArea(AlgDataPtr algData);
    void OldTrackId();

    void AddGroupHistory(DataDetTrackClassifyPtr input,
                         const std::chrono::steady_clock::time_point& dataTimePoint);
    // Fill alarm data
    void FillAlarmData(AlgDataPtr algData, DataAlarmUnit& alarmUnit);
    void FillAlarmDataTrackId(DataAlarmUnit& alarmUnit, TrackIdData& trackIdData);

    void HandTrackData(AlgDataPtr algData, DataDetTrackClassifyPtr input);

    TaskBaseArea GetArea();
    std::vector<MsgTaskArea> GetAreas();
    void AddTargetToAreaTarget(const std::string& areaId, AreaIdDataEl& areaData,
                               DataDetTrackClassifyPtr input);
    void AddAreaTargetHistory(DataDetTrackClassifyPtr input,
                              const std::chrono::steady_clock::time_point& dataTimePoint);
    void OldAreaTargetHistory();
    void CalcAreaTargetSensitity(AlgDataPtr algData);
    void HandAreaTargetData(AlgDataPtr algData, DataDetTrackClassifyPtr input);
    void AddAreaThingsHistory(AlgDataPtr algData, const std::chrono::steady_clock::time_point& dataTimePoint);
    void HandAreaThingsData(AlgDataPtr algData);

    // Save area target data
    void RecAreaTargetData(MsgRecSensitity& recSensitity, float expSensitity, AreaTargetData& areaTargetData);
    // Track target adding target in area
    void RecTrackTargetDataAreaAddTarget(MsgRecArea& recArea, int trackId, float expertSensitity,
                                         TargetSensitivityDetCount& targetCalc);
    // Save track target data
    void RecTrackTargetData(MsgRecSensitity& recSensitity, const std::string& areaId, int trackId,
                            float expertSensitity, TargetSensitivityDetCount& targetCalc);

    bool need_alarm = false;

    float orig_fps = 0.0;

    BASensitivityParam m_params;
    TaskBaseArea m_taskArea;
    std::vector<MsgTaskArea> m_areas;
    std::vector<MsgTaskArea> m_mainAreas;
    std::vector<MsgTaskArea> m_assoAreas;
    bool m_haveTrack{false};
    bool m_haveClassify{false};
    float m_taskActionsMinFps{-1.0};
    std::map<unsigned, TrackIdData> m_mapTrackIdStatus;
    std::map<std::string, AreaTargetData> m_mapAreaTargetStatus;
    OverviewRecordSensitivityRst m_overviewRecInst;
};

using SensitivityPtr = std::shared_ptr<Sensitivity>;

}  // namespace cosmo
