// PDetectorMng.h — Manager for per-task PDetector instances.

#pragma once

#include <string>

#include "flow/action/ActionInstMngBase.h"
#include "flow/detect/PDetector.h"

namespace cosmo {
class PDetectorMng : public SimpleMapActionMng<PDetector> {
public:
    PDetectorMng() : SimpleMapActionMng("PDetectorMng") {}

    PDetectorPtr GetInst(const std::string& taskId, ActionNode& action) {
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
        auto alg_inst = std::make_shared<PDetector>(taskId, action);
        inst_map[key] = alg_inst;
        return alg_inst;
    }
};
}  // namespace cosmo
