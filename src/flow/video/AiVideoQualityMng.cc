// Instance manager for AiVideoQuality actions.

#include "flow/video/AiVideoQualityMng.h"

#include <mutex>
#include <shared_mutex>
#include <string>

#include "util/Log.h"

namespace cosmo {

AiVideoQualityPtr AiVideoQualityMng::GetInst(const std::string& task_id, ActionNode& action_param) {
    auto key = task_id + action_param.flowActionId;
    {
        std::shared_lock<std::shared_mutex> lock(mtx);
        auto it = inst_map.find(key);
        if (it != inst_map.end()) {
            return it->second;
        }
    }
    std::lock_guard<std::shared_mutex> lock(mtx);
    auto it = inst_map.find(key);
    if (it != inst_map.end()) {
        return it->second;
    }
    LOG_INFO("[AiVideoQualityMng] Add:{}", task_id);
    auto alg_inst = std::make_shared<AiVideoQuality>(task_id, action_param);
    inst_map[key] = alg_inst;
    return alg_inst;
}

bool AiVideoQualityMng::DeleteInst(AiVideoQualityPtr inst, const std::string& task_id) {
    if (!inst) {
        LOG_WARN("Delete Task:{} But Inst Is Empty", task_id);
        return false;
    }
    auto flow_action_id = inst->GetFlowActionId();
    std::lock_guard<std::shared_mutex> lock(mtx);
    inst_map.erase(task_id + flow_action_id);
    LOG_INFO("Delete Task:{}", task_id);
    return true;
}

}  // namespace cosmo
