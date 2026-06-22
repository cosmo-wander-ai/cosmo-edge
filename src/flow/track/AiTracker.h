#pragma once

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "flow/action/AlgActionBase.h"
#include "flow/overview/OverviewRecordAiRst.h"
#include "flow/task/TaskBaseParam.h"
#include "infer/AiTrackerUnify.h"

namespace cosmo {
enum class TrackMotionStatusFilter {
    kNone   = 0,  // Need all results
    kStatic = 1,  // Only need moving targets
    kMove   = 2   // Only need static targets
};

// Deformation filtering
enum class TrackShapeChangeFilter {
    kNone   = 0,  // Need all results
    kStatic = 1,  // Only moving targets, filter unchanged
    kMove   = 2   // Only static targets, filter changed
};
struct AiTrackerParamUnit {
    float motion{-1.0f};  // Static threshold
    int frames{-1};       // Tracking history frame count
    float track_dynamic_match{-1.0f};
};

struct AiTrackParam {
    std::string atomic_code;
    AiTrackerParamUnit motion;  // Tracking parameters
};
class AiTracker : public AlgActionBase {
public:
    AiTracker(const std::string& taskId, ActionNode& actionParam);
    ~AiTracker();

    bool AiSdkInit(const std::string& atomicCode, const std::vector<std::string>& labels,
                   int maxWidth = media::kVideoDefaultWidth, int maxHeight = media::kVideoDefaultHeight);

    // Modify parameters - modify based on existing parameters
    bool ModifyParam(const std::string& channelId, const std::string& taskId,
                     std::vector<MsgDynamicKeyValue>& params) override;
    // Set parameters - clear previous parameters and set entirely new ones
    bool SetParam(const std::string& channelId, const std::string& taskId,
                  std::vector<MsgDynamicKeyValue>& params) override;

    // Set parameters - clear previous parameters and set entirely new ones
    bool SetArea(const std::string& channelId, const std::string& taskId, std::vector<MsgTaskArea>& areas,
                 std::vector<MsgTaskArea>& shieldedAreas) override;

    // Get history info from: start frame to: end frame
    std::vector<DataDetTrackClassify> GetHistory(const std::string& channelId, const std::string& taskId,
                                                 int64_t from, int64_t ts, int64_t to) override;

    // Get overlay info
    MsgOverviewMem GetOverviewInfo(const std::string& channelId, const std::string& taskId,
                                   int64_t streamIndex = -1, int64_t from = -1, int64_t to = -1) override;

    [[nodiscard]] std::string GetName() const {
        return name_;
    }

private:
    class TrackIdLineArea;
    class TrackIdData;
    void HandFrame(AlgDataPtr algData) override;
    bool ModifyTrackParamToLocal(AiTrackParam& trackParam, AiTrackParam& localTrackParam);
    bool SetTrackParamToLocal(AiTrackParam& trackParam);
    [[nodiscard]] bool ValidKey(const MsgDynamicKeyValue& param);
    bool AnalysisKey(MsgDynamicKeyValue& param);
    bool SetConfidenceToLocal(AiConfidence& confidence);
    void SetAtomicCode(const std::string& detAtomicCode);
    void Filter(DataDetTrackClassifyPtr data);
    void SignTargetAreas(AlgDataPtr dataPtr);
    [[nodiscard]] TargetPosition GetTaskLabelPos(const std::string& label,
                                                 const std::vector<AiLabelParam>& labelParams);
    bool SetTargetPosToLocal(const std::string& inLabel, TargetPosition pos);
    void TargetAddArea(AiDetectRstEl& target, TargetPosition pos, TargetAreaType type,
                       const MsgTaskArea& area, bool bAssociatedArea = false,
                       const std::string& mainAreaId = "");
    void TargetAddLine(AiDetectRstEl& target, TargetPosition pos, const MsgTaskArea& area,
                       bool bAssociatedArea = false, const std::string& mainAreaId = "");
    void TargetAddHistory(AlgDataPtr algData);
    void TargetCalcBreakLineArea(TrackIdData& idData, const MsgTaskArea& area);
    void TargetCalcBreakLine(AlgDataPtr algData);
    void CpBreakLineStatusToData(AlgDataPtr algData);
    void TargetOldHistory(AlgDataPtr algData);
    [[nodiscard]] TargetLineUnit TargetOnLinePos(const AiDetectRstEl& target, const std::string& areaId);
    [[nodiscard]] std::vector<AiConfidence> GetConfidence();

    // Record history aging etc. Used to get overlay info
    void RecordHistory(AlgDataPtr dataPtr);

    float CalculateNormalizedVariation(std::queue<float> ratios);

    std::string name_;
    AiTrackerUnifyPtr tracker_{nullptr};  // Detector
    int pic_width_{0};
    int pic_height_{0};

    size_t frame_index_{0};
    size_t last_frame_index_{0};
    int param_modify_sign_{0};
    int param_active_sign_{0};
    TrackMotionStatusFilter motion_status_{TrackMotionStatusFilter::kNone};
    TrackShapeChangeFilter shape_change_filter_{TrackShapeChangeFilter::kNone};
    float shape_change_threshold_{0.1f};  // Used with shape_change_filter_
    std::string atomic_code_;             // Atomic algorithm code from data stream
    std::vector<AiTrackParam> params_;    // All params, set before data stream
    AiTrackParam param_;  // Used after data stream is determined, then use only related params
    TaskBaseArea task_area_;
    bool area_have_line_{false};
    int debug_last_line_{0};
    std::vector<AiLabelParam> label_param_;  // Confidences for all algorithms
    std::map<unsigned, TrackIdData> map_track_id_status_;
    std::deque<DataDetTrackClassify> historys_;
    OverviewRecordAiRst overview_rec_inst_;
};
using AiTrackerPtr = std::shared_ptr<AiTracker>;
}  // namespace cosmo
