// FaceLogicMng — Face Logic Mng implementation.

#include "flow/logical/FaceLogicMng.h"

#include "util/Log.h"

namespace cosmo {
FaceLogicMng::FaceLogicMng() {
    LOG_INFO("{}", "FaceLogicMng Init");
}

FaceLogicMng::~FaceLogicMng() {
    LOG_INFO("{}", "FaceLogicMng Delete");
}

FaceLogicPtr FaceLogicMng::GetInst(const std::string &taskId) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    auto it = std::find_if(insts_.begin(), insts_.end(),
                           [&](const FaceLogicPtr inst) { return inst->GetTaskId() == taskId; });

    if (it != insts_.end()) {
        return *it;  // Channel has been created
    }

    return nullptr;
}

FaceLogicPtr FaceLogicMng::GetInst(const std::string &taskId, ActionNode &actionFaceLogic) {
    auto inst = GetInst(taskId);
    if (inst) {
        return inst;
    }
    std::lock_guard<std::shared_mutex> lock(mtx_);
    inst = std::make_shared<FaceLogic>(taskId, actionFaceLogic);
    insts_.push_back(inst);
    return inst;
}

bool FaceLogicMng::DeleteInst(FaceLogicPtr inst) {
    if (!inst) {
        return false;
    }
    std::lock_guard<std::shared_mutex> lock(mtx_);
    auto it = std::find_if(insts_.begin(), insts_.end(),
                           [&](const FaceLogicPtr instL) { return instL->GetTaskId() == inst->GetTaskId(); });
    if (it != insts_.end()) {
        insts_.erase(it);
        return true;
    }

    return false;
}

void FaceLogicMng::QueueStatus(std::vector<AlgActionDataQueueStatus> &queStatus, unsigned int durationSec) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    for (auto &inst : insts_) {
        inst->QueueStatus(queStatus, durationSec);
    }
}

void FaceLogicMng::ActionInfo(std::vector<ActionRuntimeInfo> &actionInfos) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    for (auto &inst : insts_) {
        inst->ActionInfo(actionInfos);
    }
}
}  // namespace cosmo