// PDetector.h — Per-task YOLO detector for picture/snapshot mode.

#pragma once

#include <list>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "flow/action/PActionBase.h"
#include "flow/task/TaskBaseParam.h"
#include "infer/AiDetectorUnify.h"
#include "util/DurationStat.h"
#include "util/GeometricPos.h"

namespace cosmo {
struct PDetectorParam {
    std::vector<AiConfidence> confidence;
};
class PDetector : public PActionBase {
public:
    PDetector(const std::string& taskId, ActionNode& action);
    ~PDetector() override;

    bool ActionInit() override;
    util::ErrorEnum HandPic(AlgDataPtr algData) override;

    // Modify parameters — update on top of existing parameters
    bool ModifyParam(const std::string& taskId, std::vector<MsgDynamicKeyValue>& params) override;
    // Set parameters — clear previous parameters, set all new parameters
    bool SetParam(const std::string& taskId, std::vector<MsgDynamicKeyValue>& params) override;
    // Set areas — clear previous areas, set all new areas
    bool SetArea(const std::string& taskId, std::vector<MsgTaskArea>& areas,
                 std::vector<MsgTaskArea>& shieldedAreas) override;

private:
    [[nodiscard]] bool ValidKey(const MsgDynamicKeyValue& param) const;
    bool AnalysisKey(const MsgDynamicKeyValue& param);

private:
    std::shared_mutex mtx_;
    AiDetectorUnifyPtr inst_;  // Detector inference instance
    size_t handle_frames_{0};
    size_t filter_frames_{0};
    PDetectorParam params_;
    TaskBaseArea task_area_;
    util::ErrorEnum action_status_{util::ErrorEnum::Success};
    util::DurationStat duration_stat_;
};
using PDetectorPtr = std::shared_ptr<PDetector>;
}  // namespace cosmo
