// AiRecognizerMng — AiRecognizerMng — Instance manager for AiRecognizer actions.

#include "flow/recognizer/AiRecognizerMng.h"

#include "util/Log.h"

namespace cosmo {
AiRecognizerMng::AiRecognizerMng() {
    LOG_INFO("{}", "AiRecognizerMng Init");
}

AiRecognizerMng::~AiRecognizerMng() {
    LOG_INFO("{}", "AiRecognizerMng Delete");
}

AiRecognizerPtr AiRecognizerMng::GetInst(const std::string& task_id, const std::string& alg_code,
                                         ActionNode& action_node) {
    LOG_INFO("[AiRecognizerMng] Add:{}", task_id);
    std::lock_guard<std::shared_mutex> lock(mtx_);
    // Each algorithm has an instance map, find the instance map for this algorithm
    auto inst = insts_[task_id + alg_code];
    if (inst)
        return inst;
    LOG_INFO("[AiRecognizerMng] Add:{} algCode:{}", task_id, alg_code);
    auto alg_inst = std::make_shared<AiRecognizer>(task_id, alg_code, action_node);
    LOG_INFO("[AiRecognizerMng] Add:{}", task_id);
    insts_[task_id + alg_code] = alg_inst;
    return alg_inst;
}

bool AiRecognizerMng::DeleteInst(AiRecognizerPtr inst, const std::string& task_id,
                                 const std::string& alg_code) {
    if (!inst) {
        LOG_WARN("Delete Task:{} algCode:{} But Inst Is Empty", task_id, alg_code);
        return false;
    }

    std::lock_guard<std::shared_mutex> lock(mtx_);
    insts_.erase(task_id + alg_code);

    LOG_INFO("Delete Task:{} algCode:{}", task_id, alg_code);
    return true;
}

void AiRecognizerMng::QueueStatus(std::vector<AlgActionDataQueueStatus>& que_status,
                                  unsigned int duration_sec) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    for (auto it = insts_.begin(); it != insts_.end(); it++) {
        it->second->QueueStatus(que_status, duration_sec);
    }
}

void AiRecognizerMng::ActionInfo(std::vector<ActionRuntimeInfo>& action_infos) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    for (auto it = insts_.begin(); it != insts_.end(); it++) {
        it->second->ActionInfo(action_infos);
    }
}
}  // namespace cosmo