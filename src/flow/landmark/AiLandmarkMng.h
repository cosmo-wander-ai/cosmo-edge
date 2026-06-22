// Instance manager for AiLandmark video stream actions.

#pragma once

#include <shared_mutex>
#include <string>

#include "flow/action/ActionInstMngBase.h"
#include "flow/landmark/AiLandmark.h"

namespace cosmo {
class AiLandmarkMng : public SimpleMapActionMng<AiLandmark> {
public:
    AiLandmarkMng() : SimpleMapActionMng("AiLandmarkMng") {}

    [[nodiscard]] AiLandmarkPtr GetInst(const std::string& task_id, ActionNode& action) {
        auto key = task_id + action.flowActionId;

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
        LOG_INFO("[AiLandmarkMng] Add:{}/{} algCode:{}", task_id, action.flowActionId, action.atomicCode);
        auto alg_inst = std::make_shared<AiLandmark>(task_id, action.atomicCode, action);
        inst_map[key] = alg_inst;
        return alg_inst;
    }
};
}  // namespace cosmo
