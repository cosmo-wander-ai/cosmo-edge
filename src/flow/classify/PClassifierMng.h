// Instance manager for PClassifier actions (keyed by taskId + flowActionId).

#pragma once

#include <string>

#include "flow/action/ActionInstMngBase.h"
#include "flow/classify/PClassifier.h"

namespace cosmo {
class PClassifierMng : public SimpleMapActionMng<PClassifier> {
public:
    PClassifierMng() : SimpleMapActionMng("PClassifierMng") {}

    PClassifierPtr GetInst(const std::string& taskId, ActionNode& action) {
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
        LOG_INFO("Add Task:{} algCode:{}", taskId, action.atomicCode);
        auto algInst  = std::make_shared<PClassifier>(taskId, action);
        inst_map[key] = algInst;
        return algInst;
    }
};
}  // namespace cosmo
