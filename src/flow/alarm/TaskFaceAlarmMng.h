// TaskFaceAlarmMng — face alarm task management.
#pragma once

#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "flow/alarm/TaskFaceAlarm.h"

namespace cosmo {
class TaskFaceAlarmMng {
public:
    TaskFaceAlarmMng();
    virtual ~TaskFaceAlarmMng();

    TaskFaceAlarmPtr GetInst(const std::string& channelId, const std::string& taskId,
                             const std::string& algId, const std::string& algName, ActionNode& action);
    bool DeleteInst(TaskFaceAlarmPtr Inst);

    void QueueStatus(std::vector<AlgActionDataQueueStatus>& queStatus, unsigned int durationSec = 30);
    void ActionInfo(std::vector<ActionRuntimeInfo>& actionInfo);

private:
    std::shared_mutex m_mtx;
    std::vector<TaskFaceAlarmPtr> m_insts;
};
}  // namespace cosmo
