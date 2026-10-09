// FaceLogicMng — face logic management.
#pragma once

#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "flow/logical/FaceLogic.h"

namespace cosmo {
class FaceLogicMng {
public:
    FaceLogicMng();
    virtual ~FaceLogicMng();

    FaceLogicPtr GetInst(const std::string &taskId, ActionNode &actionFaceLogic);
    bool DeleteInst(FaceLogicPtr inst);

    void QueueStatus(std::vector<AlgActionDataQueueStatus> &queStatus, unsigned int durationSec = 30);
    void ActionInfo(std::vector<ActionRuntimeInfo> &actionInfo);

private:
    FaceLogicPtr GetInst(const std::string &taskId);

    std::shared_mutex mtx_;
    std::vector<FaceLogicPtr> insts_;
};
}  // namespace cosmo
