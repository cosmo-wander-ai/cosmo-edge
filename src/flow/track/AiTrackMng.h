#pragma once

#include <string>

#include "flow/action/ActionInstMngBase.h"
#include "flow/track/AiTracker.h"

namespace cosmo {
class AiTrackMng : public MapActionMng<AiTracker> {
public:
    AiTrackMng() : MapActionMng("AiTrackMng") {}

    [[nodiscard]] AiTrackerPtr GetInst(const std::string& taskId, ActionNode& actionParam) {
        {
            std::shared_lock<std::shared_mutex> lock(mtx);
            auto it = inst_map.find(taskId);
            if (it != inst_map.end() && it->second) {
                return it->second;
            }
        }
        std::lock_guard<std::shared_mutex> lock(mtx);
        auto it = inst_map.find(taskId);
        if (it != inst_map.end() && it->second) {
            return it->second;
        }
        auto tracker = std::make_shared<AiTracker>(taskId, actionParam);
        LOG_INFO("[AiTrackMng] Add:{}", taskId);
        inst_map[taskId] = tracker;
        return tracker;
    }

    [[nodiscard]] bool DeleteInst(AiTrackerPtr inst, const std::string& taskId) {
        if (!inst) {
            return false;
        }
        std::lock_guard<std::shared_mutex> lock(mtx);
        inst_map.erase(taskId);
        return true;
    }
};
}  // namespace cosmo
