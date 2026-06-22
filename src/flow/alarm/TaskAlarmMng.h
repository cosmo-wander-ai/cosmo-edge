// TaskAlarmMng.h — TaskAlarm instance manager.

#pragma once

#include <string>

#include "flow/action/ActionInstMngBase.h"
#include "flow/alarm/TaskAlarm.h"

namespace cosmo {
class TaskAlarmMng : public VectorActionMng<TaskAlarm> {
public:
    TaskAlarmMng() : VectorActionMng("TaskAlarmMng") {}

    // Return existing instance; return nullptr if not found
    TaskAlarmPtr GetInst(const std::string& /*channelId*/, const std::string& taskId) {
        return FindByTaskId(taskId);
    }

    // Return existing instance, or create a new one if not found
    TaskAlarmPtr GetInst(const std::string& channelId, const std::string& taskId, ActionNode& action) {
        auto inst = FindByTaskId(taskId);
        if (inst) {
            return inst;
        }
        std::lock_guard<std::shared_mutex> lock(mtx);
        inst = FindByTaskIdUnlocked(taskId);
        if (inst) {
            return inst;
        }
        auto newInst = std::make_shared<TaskAlarm>(channelId, taskId, action);
        insts.push_back(newInst);
        return newInst;
    }
};
}  // namespace cosmo
