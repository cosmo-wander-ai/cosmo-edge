// AiVideoQuality header.

#pragma once

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "flow/action/AlgActionBase.h"
#include "flow/overview/OverviewRecordAiRst.h"
#include "flow/task/TaskBaseParam.h"
#include "infer/AiVideoQualityUnify.h"

namespace cosmo {

struct AiVideoQualityParam {
    infer::AiVideoQualityType type{infer::AiVideoQualityType::kBlur};  // Input type
    bool is_param_changed{false};
    float threshold{-1.0f};
    float threshold_ext{-1.0f};
};

class AiVideoQuality : public AlgActionBase {
public:
    AiVideoQuality(const std::string& task, ActionNode& action_param);
    ~AiVideoQuality() override;

    AiVideoQuality(const AiVideoQuality&)            = delete;
    AiVideoQuality& operator=(const AiVideoQuality&) = delete;

    bool AiSdkInit();

    bool AnalysisKey(const MsgDynamicKeyValue& param);
    bool ModifyParam(const std::string& channel_id, const std::string& task_id,
                     std::vector<MsgDynamicKeyValue>& params) override;
    bool SetParam(const std::string& channel_id, const std::string& task_id,
                  std::vector<MsgDynamicKeyValue>& params) override;

    bool SetArea(const std::string& channel_id, const std::string& task_id, std::vector<MsgTaskArea>& areas,
                 std::vector<MsgTaskArea>& shielded_areas) override;

    [[nodiscard]] std::string GetFlowActionId() const {
        return action_info_.flowActionId;
    }

    // Get overlay info
    MsgOverviewMem GetOverviewInfo(const std::string& channel_id, const std::string& task_id,
                                   int64_t stream_index = -1, int64_t from = -1, int64_t to = -1) override;

private:
    void HandFrame(AlgDataPtr alg_data) override;

    [[nodiscard]] std::vector<MsgTaskArea> GetAssoAreas(const std::vector<MsgTaskArea>& areas) const;
    TaskBaseArea GetArea();

    ActionNode action_info_;
    infer::AiVideoQualityUnifyPtr inst_;  // Classifier

    int pic_width_{0};
    int pic_height_{0};
    AiVideoQualityParam params_;
    TaskBaseArea task_area_;
    OverviewRecordAiRst overview_rec_inst_;
};
using AiVideoQualityPtr = std::shared_ptr<AiVideoQuality>;
}  // namespace cosmo
