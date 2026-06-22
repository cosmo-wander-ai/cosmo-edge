#pragma once

#include <string>

#include "flow/action/ActionInstMngBase.h"
#include "flow/logical/PLogicalJudgment.h"

namespace cosmo {
class PLogicalJudgmentMng : public SimpleMapActionMng<PLogicalJudgment> {
public:
    PLogicalJudgmentMng() : SimpleMapActionMng("PLogicalJudgmentMng") {}

    [[nodiscard]] PLogicalJudgmentPtr GetInst(const std::string& taskId, ActionNode& action) {
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
        auto algInst  = std::make_shared<PLogicalJudgment>(taskId, action);
        inst_map[key] = algInst;
        return algInst;
    }
};
}  // namespace cosmo
