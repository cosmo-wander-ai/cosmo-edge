// Instance manager for AiClassifierGroup actions (keyed by taskId + flowActionId).

#pragma once

#include <string>

#include "flow/action/ActionInstMngBase.h"
#include "flow/classify/AiClassifierGroup.h"

namespace cosmo {
class AiClassifyGroupMng : public SimpleMapActionMng<AiClassifierGroup> {
public:
    AiClassifyGroupMng() : SimpleMapActionMng("AiClassifyGroupMng") {}

    AiClassifierGroupPtr GetInst(const std::string& taskId, ActionNode& action) {
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
        LOG_INFO("[AiClassifyGroupMng] Add:{} flowActionId:{} atomicCode:{}", taskId, action.flowActionId,
                 action.atomicCode);
        auto classifier = std::make_shared<AiClassifierGroup>(taskId, action);
        inst_map[key]   = classifier;
        return classifier;
    }
};
}  // namespace cosmo
