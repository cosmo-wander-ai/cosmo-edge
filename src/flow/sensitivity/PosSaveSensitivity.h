// Positive detection save, report if positive detection sensitivity does not meet standard
// within a period
// Sensitivity calculation - report only if sensitivity does not meet standard within the cycle

#pragma once

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "flow/action/AlgActionBase.h"
#include "flow/common/AlgDataRecord.h"
#include "flow/overview/OverviewRecordBehaviorNoneSenRst.h"
#include "flow/task/TaskBaseParam.h"
#include "util/MsgDynamicElement.h"

namespace cosmo {

struct BAPosSaveSensitivityParam {
    int pos_sen_real_time_enable{0};         // Real-time alarm, no need to wait for track to disappear
    uint64_t pos_sen_duration{3000};         // Detection time in ms
    uint64_t pos_sen_duration_time{3000};    // Detection time in ms
    uint64_t pos_sen_duration_time_type{1};  // Time coefficient
    float sensitivity_ratio{0.5f};           // Sensitivity ratio
    size_t pos_sen_hit_count{5};             // Hit count
    size_t pos_sen_total_count{10};          // Total count for sensitivity calculation
};

class PosSaveSensitivity : public AlgActionBase {
public:
    // taskActionsMinFps: minimum frame rate in the orchestrated processes
    PosSaveSensitivity(const std::string& taskId, ActionNode& action);
    ~PosSaveSensitivity();

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

    void HandFrame(AlgDataPtr algData) override;

private:
    class TrackIdData;
    class TargetSensitivityDetCount;

    bool AnalysisKey(MsgDynamicKeyValue& param, BAPosSaveSensitivityParam& localParamEl);

    void TrackData2RecData(TrackIdData& idData, MsgRecPosSaveSensitityTarget& recTarget);
    void HandTrackAlarm(AlgDataPtr algData, TrackIdData& idData, const std::string& tag);
    void AssoTarget(AlgDataPtr algData, TrackIdData& idData);
    void AddHistory(AlgDataPtr algData, DataDetTrackClassifyPtr input);
    void OldHistory();
    void CalcSensitity(AlgDataPtr algData);
    void OldTrackId(AlgDataPtr algData);

    // Fill alarm data
    void FillAlarmData(AlgDataPtr algData, DataAlarmUnit& alarmUnit);
    void FillAlarmDataTrackId(DataAlarmUnit& alarmUnit, TrackIdData& trackIdData);

    void HandTrackData(AlgDataPtr algData, DataDetTrackClassifyPtr input);

    [[nodiscard]] TaskBaseArea GetArea();

    ActionNode action_info_;
    bool should_alarm_{false};
    size_t stream_index_{0};
    size_t frame_index_{0};
    size_t timestamp_{0};
    int width_{-1};
    int height_{-1};
    BAPosSaveSensitivityParam params_;
    VideoFramePtr dec_frame_{nullptr};
    TaskBaseArea task_area_;
    bool has_track_{false};
    bool has_classify_{false};
    std::map<unsigned, TrackIdData> map_track_id_status_;
    OverviewRecordBehaviorNoneSenRst overview_rec_inst_;
};
using PosSaveSensitivityPtr = std::shared_ptr<PosSaveSensitivity>;
}  // namespace cosmo
