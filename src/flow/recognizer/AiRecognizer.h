// AiRecognizer — Face/body recognition action with feature comparison.

#pragma once

#include <deque>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "flow/action/AlgActionBase.h"
#include "flow/common/AlgDataQueueDistributor.h"
#include "flow/overview/OverviewRecordAiRst.h"
#include "flow/task/TaskBaseParam.h"
#include "infer/AiRecognizerInterface.h"
#include "util/DurationStat.h"

namespace cosmo {
enum class MatchFlagType {
    NotMatch = 0,  // Alarm on no match
    Match,         // Alarm on match
    All,           // Always alarm
    Max,
};
constexpr bool IsValidMatchFlagType(int value) {
    return value >= static_cast<int>(MatchFlagType::NotMatch) && value < static_cast<int>(MatchFlagType::Max);
}

struct AiRecognizerAlgParam {
    FeatureInputType feature_input{FeatureInputType::Face};
    MatchFlagType match_flag{MatchFlagType::Match};
    float limit_score{-1.0};
};

struct AiRecognizerParam {
    std::vector<std::string> face_set;
};

class AiRecognizer : public AlgActionBase, public AlgDataQueueDistributor {
public:
    AiRecognizer(const std::string& init_task_id, const std::string& alg_code, ActionNode& action_param);
    ~AiRecognizer();

    bool AiSdkInit();

    bool RegistTaskQueue(AlgTaskUnit& param) override;
    bool RemoveTaskQueue(AlgTaskUnit& param) override;
    int ForceRemoveByTaskId(const std::string& taskId) override;
    void QueueStatus(std::vector<AlgActionDataQueueStatus>& que_status,
                     unsigned int duration_sec = 30) override;
    void ActionInfo(std::vector<ActionRuntimeInfo>& action_info) override;

    bool AnalysisKey(MsgDynamicKeyValue& param);
    bool ModifyParam(const std::string& channel_id, const std::string& task_id,
                     std::vector<MsgDynamicKeyValue>& params) override;
    bool SetParam(const std::string& channel_id, const std::string& task_id,
                  std::vector<MsgDynamicKeyValue>& params) override;
    bool SetArea(const std::string& channel_id, const std::string& area_task_id,
                 std::vector<MsgTaskArea>& areas, std::vector<MsgTaskArea>& shielded_areas) override;

    MsgOverviewMem GetOverviewInfo(const std::string& channel_id, const std::string& overview_task_id,
                                   int64_t query_stream_index = -1, int64_t from = -1,
                                   int64_t to = -1) override;

protected:
    void HandFrame(AlgDataPtr alg_data) override;

private:
    void HandFace(AlgDataPtr alg_data);
    bool GetRecodResult(bool compare_rst, size_t count, AiDetectMatchHighScoreInfo& match_info);
    std::shared_mutex mtx_;
    std::string alg_code_;
    ActionNode action_info_;
    AiRecognizerInterfacePtr inst_;
    size_t filter_frames_{0};
    int pic_width_{0};
    int pic_height_{0};
    AiRecognizerAlgParam alg_params_;
    AiRecognizerParam params_;
    TaskBaseArea task_area_;
    util::DurationStat duration_stat_;
    OverviewRecordAiRst overview_rec_inst_;
    std::deque<DataDetTrackClassify> historys_;
};

using AiRecognizerPtr = std::shared_ptr<AiRecognizer>;
}  // namespace cosmo
