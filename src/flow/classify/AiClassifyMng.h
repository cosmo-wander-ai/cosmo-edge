// Instance manager for AiClassifier actions (keyed by taskId + atomicCode).

#pragma once

#include <string>

#include "flow/action/ActionInstMngBase.h"
#include "flow/classify/AiClassifier.h"

namespace cosmo {
class AiClassifyMng : public MapActionMng<AiClassifier> {
public:
    AiClassifyMng() : MapActionMng("AiClassifyMng") {}

    AiClassifierPtr GetInst(const std::string& taskId, ActionNode& action) {
        auto key = taskId + action.atomicCode;

        {
            std::shared_lock<std::shared_mutex> lock(mtx);

            auto it = inst_map.find(key);

            if (it != inst_map.end() && it->second) {
                return it->second;
            }
        }

        std::lock_guard<std::shared_mutex> lock(mtx);

        auto it = inst_map.find(key);

        if (it != inst_map.end() && it->second) {
            return it->second;
        }
        LOG_INFO("[AiClassifyMng] Add:{} algCode:{}", taskId, action.atomicCode);
        auto classifier = std::make_shared<AiClassifier>(taskId, action);
        inst_map[key]   = classifier;
        return classifier;
    }

    bool DeleteInst(AiClassifierPtr inst) {
        if (!inst) {
            return false;
        }
        std::lock_guard<std::shared_mutex> lock(mtx);
        inst_map.erase(inst->GetTaskId() + inst->GetAtomicCode());
        LOG_INFO("Delete Task:{} algCode:{}", inst->GetTaskId(), inst->GetAtomicCode());
        return true;
    }
};
}  // namespace cosmo
