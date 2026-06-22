// LinkAgeTask — Link Age Task implementation.

#include "linkage/LinkAgeTask.h"

#include "linkage/LinkAgeAlarm.h"
#include "linkage/LinkAgeAudioDevice.h"
#include "util/Keys.h"
#include "util/Log.h"

namespace cosmo::linkage {
LinkAgeTask::LinkAgeTask(const std::string& name, LinkageStrategyWorkflow& strategy) : task_name_(name) {
    std::vector<LinkAgeBasePtr> actions;
    for (auto& workflow : strategy.workflow) {
        if (kLaAlarmDataCode == workflow.action_id) {
            auto action_inst = std::make_shared<LinkAgeAlarm>(workflow);
            if (key::alg::ACTION_ROOT_VALUE == workflow.preFlowActionId) {
                task_.task = action_inst;
                LOG_INFO("[]: ROOT IS:{}/{}", task_name_, workflow.action_id, workflow.action_name);
            } else {
                actions.push_back(action_inst);
            }

        } else if (kLaAudioDeviceCode == workflow.action_id) {
            auto action_inst = std::make_shared<LinkAgeAudioDevice>(workflow);
            if (key::alg::ACTION_ROOT_VALUE == workflow.preFlowActionId) {
                task_.task = action_inst;
                LOG_INFO("[]: ROOT IS:{}/{}", task_name_, workflow.action_id, workflow.action_name);
            } else {
                actions.push_back(action_inst);
            }
        } else {
            LOG_WARN("[{}] {}/{} Not Support", task_name_, workflow.action_id, workflow.action_name);
        }
    }
    OrganizeTasks(task_, actions);
    LOG_INFO("[{}] Have Tasks:{}", task_name_, strategy.workflow.size());
}

LinkAgeTask::~LinkAgeTask() {
    LOG_INFO("[{}] LinkAgeTask Delete", task_name_);
}

std::vector<LinkAgeBasePtr> LinkAgeTask::FindPres(std::vector<LinkAgeBasePtr>& actions,
                                                  const std::string& flow_action_id) {
    std::vector<LinkAgeBasePtr> extracted;
    auto it = actions.begin();
    while (it != actions.end()) {
        if ((*it)->GetPreFlowActionId() == flow_action_id) {
            extracted.push_back(*it);
            it = actions.erase(it);  // Erase and return next iterator
        } else {
            ++it;
        }
    }
    return extracted;
}

void LinkAgeTask::AddSons(LinkAgeTaskUnit& unit, const std::vector<LinkAgeBasePtr>& sons) {
    if (sons.empty()) {
        return;
    }

    for (const auto& son : sons) {
        LinkAgeTaskUnit task;
        task.task = son;
        unit.sons.push_back(task);
    }
}

void LinkAgeTask::OrganizeTasks(LinkAgeTaskUnit& unit, std::vector<LinkAgeBasePtr>& actions) {
    if (!unit.task) {
        return;
    }
    auto extracted = FindPres(actions, unit.task->GetFlowActionId());
    AddSons(unit, extracted);
    if (actions.empty()) {
        return;
    }

    for (auto& son : unit.sons) {
        OrganizeTasks(son, actions);
    }
}

void LinkAgeTask::DoTaskAlarm(LinkAgeTaskUnit& task_unit, const std::string& channel_id,
                              const std::string& alg_id) {
    if (task_unit.task) {
        if (!task_unit.task->DoAlarm(channel_id, alg_id)) {
            return;
        }
        LOG_INFO("Action {}/{} FlowId:{} Alarm", task_unit.task->GetActionId(), task_unit.task->GetName(),
                 task_unit.task->GetFlowActionId());
        for (auto& son : task_unit.sons) {
            DoTaskAlarm(son, channel_id, alg_id);
        }
    }
}

void LinkAgeTask::DoAlarm(const std::string& channel_id, const std::string& alg_id) {
    DoTaskAlarm(task_, channel_id, alg_id);
}

bool LinkAgeTask::IsAudioDeviceInUse(const LinkAgeTaskUnit& task_unit, const std::string& dev_id) const {
    if (task_unit.task) {
        if (task_unit.task->IsAudioDeviceInUse(dev_id)) {
            return true;
        }
        for (const auto& son : task_unit.sons) {
            if (IsAudioDeviceInUse(son, dev_id)) {
                return true;
            }
        }
    }

    return false;
}

bool LinkAgeTask::IsAudioDeviceInUse(const std::string& dev_id) {
    return IsAudioDeviceInUse(task_, dev_id);
}

bool LinkAgeTask::IsAudioFileInUse(const LinkAgeTaskUnit& task_unit, const std::string& dev_id) const {
    if (task_unit.task) {
        if (task_unit.task->IsAudioFileInUse(dev_id)) {
            return true;
        }
        for (const auto& son : task_unit.sons) {
            if (IsAudioFileInUse(son, dev_id)) {
                return true;
            }
        }
    }

    return false;
}

bool LinkAgeTask::IsAudioFileInUse(const std::string& dev_id) {
    return IsAudioFileInUse(task_, dev_id);
}
}  // namespace cosmo::linkage
