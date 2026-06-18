// AI classifier action with area-based classification.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "flow/classify/AiClassifierBase.h"
#include "flow/task/TaskBaseParam.h"

namespace cosmo {
struct AAClassifyAreaParam {
    MsgInputAreaType input_area_type{MsgInputAreaType::All};  // Input detection area type
};

class AiClassifierArea : public AiClassifierBase {
public:
    AiClassifierArea(const std::string& taskId, ActionNode& action);
    ~AiClassifierArea() = default;

    // Set areas - clears previous areas and applies a full replacement.
    bool SetArea(const std::string& channelId, const std::string& taskId, std::vector<MsgTaskArea>& areas,
                 std::vector<MsgTaskArea>& shieldedAreas) override;

private:
    std::vector<MsgTaskArea> GetAssoAreas(std::vector<MsgTaskArea> areas);
    std::vector<MsgTaskArea> GetAreas();
    std::vector<AiDetectRstEl> GenTargets();

    bool CheckDataAvailable(AlgDataPtr algData) override;
    void HandFramesEx(std::vector<AlgDataPtr> algDatas) override;

    int width_{0};
    int height_{0};
    AAClassifyAreaParam params_;
    std::vector<MsgTaskArea> task_areas_;
};
using AiClassifierAreaPtr = std::shared_ptr<AiClassifierArea>;
}  // namespace cosmo
