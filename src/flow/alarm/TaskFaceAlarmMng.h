#pragma once

/*
 * @Author: zhangxiaobo
 * @Date: 2023-11-13 19:22:44
 * @LastEditors: zhangxiaobo
 * @LastEditTime: 2024-11-14 11:01:07
 * @Descripttion:
 */

#include <map>
#include <memory>
#include <string>

#include "flow/action/ActionInstMngBase.h"
#include "flow/alarm/TaskFaceAlarm.h"

namespace cosmo {
class TaskFaceAlarmMng : public IMngStatusProvider {
public:
    TaskFaceAlarmMng();
    ~TaskFaceAlarmMng();

    TaskFaceAlarmPtr GetInst(const std::string& channelId, const std::string& taskId,
                             const std::string& algId, const std::string& algName, ActionNode& action);
    bool DeleteInst(TaskFaceAlarmPtr Inst);

    void QueueStatus(std::vector<AlgActionDataQueueStatus>& queStatus,
                     unsigned int durationSec = 30) override;
    void ActionInfo(std::vector<ActionRuntimeInfo>& actionInfo) override;

private:
    std::shared_mutex m_mtx;
    std::vector<TaskFaceAlarmPtr> m_insts;
};
}  // namespace cosmo
