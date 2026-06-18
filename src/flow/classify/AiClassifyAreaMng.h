// Instance manager for AiClassifierArea actions (keyed by taskId + flowActionId).

#pragma once

#include <string>

#include "flow/action/ActionInstMngBase.h"
#include "flow/classify/AiClassifierArea.h"

namespace cosmo {
class AiClassifyAreaMng : public SimpleMapActionMng<AiClassifierArea> {
public:
    AiClassifyAreaMng() : SimpleMapActionMng("AiClassifyAreaMng") {}

    AiClassifierAreaPtr GetInst(const std::string& taskId, ActionNode& action) {
        auto key = taskId + action.flowActionId;

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
        LOG_INFO("[AiClassifyAreaMng] Add:{} flowActionId:{} atomicCode:{}", taskId, action.flowActionId,
                 action.atomicCode);
        auto classifier = std::make_shared<AiClassifierArea>(taskId, action);
        inst_map[key]   = classifier;
        return classifier;
    }
};
}  // namespace cosmo
