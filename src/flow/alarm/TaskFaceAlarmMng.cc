// TaskFaceAlarmMng.cc — Face alarm manager.

#include "flow/alarm/TaskFaceAlarmMng.h"

#include "util/Log.h"

namespace cosmo {
TaskFaceAlarmMng::TaskFaceAlarmMng() {
    LOG_INFO("{}", "TaskFaceAlarmMng Init");
}

TaskFaceAlarmMng::~TaskFaceAlarmMng() {
    LOG_INFO("{}", "TaskFaceAlarmMng Delete");
}

TaskFaceAlarmPtr TaskFaceAlarmMng::GetInst(const std::string &channelId, const std::string &taskId,
                                           const std::string &algId, const std::string &algName,
                                           ActionNode &action) {
    std::lock_guard<std::shared_mutex> lock(m_mtx);
    auto it = std::find_if(m_insts.begin(), m_insts.end(),
                           [&](const TaskFaceAlarmPtr inst) { return inst->GetTaskId() == taskId; });

    if (it != m_insts.end()) {
        return *it;  // Instance already created
    }

    auto inst = std::make_shared<TaskFaceAlarm>(channelId, taskId, algId, algName, action);
    m_insts.push_back(inst);
    return inst;
}

bool TaskFaceAlarmMng::DeleteInst(TaskFaceAlarmPtr inst) {
    if (!inst) {
        return false;
    }
    std::lock_guard<std::shared_mutex> lock(m_mtx);
    auto it = std::find_if(m_insts.begin(), m_insts.end(),
                           [&](const TaskFaceAlarmPtr el) { return el->GetTaskId() == inst->GetTaskId(); });
    if (it != m_insts.end()) {
        m_insts.erase(it);
        return true;
    }

    return false;
}

void TaskFaceAlarmMng::QueueStatus(std::vector<AlgActionDataQueueStatus> &queStatus,
                                   unsigned int durationSec) {
    std::shared_lock<std::shared_mutex> lock(m_mtx);
    for (auto &inst : m_insts) {
        inst->QueueStatus(queStatus, durationSec);
    }
}

void TaskFaceAlarmMng::ActionInfo(std::vector<ActionRuntimeInfo> &actionInfos) {
    std::shared_lock<std::shared_mutex> lock(m_mtx);
    for (auto &inst : m_insts) {
        inst->ActionInfo(actionInfos);
    }
}

}  // namespace cosmo