#pragma once

#include <string>

#include "flow/action/ActionInstMngBase.h"
#include "flow/recognizer/PRecognizer.h"

namespace cosmo {
class PRecognizerMng : public SimpleMapActionMng<PRecognizer> {
public:
    PRecognizerMng() : SimpleMapActionMng("PRecognizerMng") {}

    PRecognizerPtr GetInst(const std::string& task_id, ActionNode& action) {
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
        LOG_INFO("Add Task:{} algCode:{}", task_id, action.atomicCode);
        auto alg_inst = std::make_shared<PRecognizer>(task_id, action);
        inst_map[key] = alg_inst;
        return alg_inst;
    }
};
}  // namespace cosmo
