// AI landmark keypoint detection action for video stream pipeline.

#pragma once

#include <list>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "flow/action/AlgActionBase.h"
#include "flow/task/TaskBaseParam.h"
#include "infer/AiLandmarkInterface.h"

namespace cosmo {

class AiLandmark : public AlgActionBase {
public:
    AiLandmark(const std::string& a_task_id, const std::string& alg_code, ActionNode& action);
    ~AiLandmark() override;

    AiLandmark(const AiLandmark&)            = delete;
    AiLandmark& operator=(const AiLandmark&) = delete;

    [[nodiscard]] bool AiSdkInit();

    bool SetArea(const std::string& channel_id, const std::string& task_id, std::vector<MsgTaskArea>& areas,
                 std::vector<MsgTaskArea>& shielded_areas) override;

private:
    void HandFrame(AlgDataPtr alg_data) override;

    std::string alg_code_;
    AiLandmarkInterfacePtr inst_;  // Landmark inference instance
    std::shared_mutex mtx_;
    TaskBaseArea task_area_;
};
using AiLandmarkPtr = std::shared_ptr<AiLandmark>;
}  // namespace cosmo
