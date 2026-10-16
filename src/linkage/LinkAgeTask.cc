// LinkAgeTask — Link Age Task implementation.

#include "linkage/LinkAgeTask.h"

#include <algorithm>

#include "linkage/LinkAgeAlarm.h"
#include "linkage/LinkAgeAlarmOutput.h"
#include "linkage/LinkAgeAudioDevice.h"
#include "util/Keys.h"
#include "util/Log.h"

namespace cosmo::linkage {
namespace {

    LinkAgeBasePtr CreateAction(LinkAgeParamNode& action,
                                const std::shared_ptr<AlarmOutputController>& outputs) {
        switch (ClassifyLinkAgeActionId(action.action_id)) {
            case LinkAgeActionKind::kAlarm:
                return std::make_shared<LinkAgeAlarm>(action);
            case LinkAgeActionKind::kAudioDevice:
                return std::make_shared<LinkAgeAudioDevice>(action);
            case LinkAgeActionKind::kAlarmOutput:
                return std::make_shared<LinkAgeAlarmOutput>(action, outputs);
            case LinkAgeActionKind::kUnsupported:
                return nullptr;
        }
        return nullptr;
    }

    template <typename Predicate>
    bool AnyActionMatches(const LinkAgeTaskUnit& unit, const Predicate& predicate) {
        if (!unit.task) {
            return false;
        }
        if (predicate(*unit.task)) {
            return true;
        }
        return std::any_of(unit.sons.begin(), unit.sons.end(),
                           [&](const auto& son) { return AnyActionMatches(son, predicate); });
    }

}  // namespace

LinkAgeTask::LinkAgeTask(const std::string& name, LinkageStrategyWorkflow& strategy,
                         std::shared_ptr<AlarmOutputController> outputs)
    : task_name_(name) {
    std::vector<LinkAgeBasePtr> actions;
    for (auto& workflow : strategy.workflow) {
        auto action_inst = CreateAction(workflow, outputs);
        if (!action_inst) {
            LOG_WARN("[{}] {}/{} Not Support", task_name_, workflow.action_id, workflow.action_name);
            continue;
        }
        if (key::alg::ACTION_ROOT_VALUE == workflow.preFlowActionId) {
            task_.task = action_inst;
            LOG_INFO("[]: ROOT IS:{}/{}", task_name_, workflow.action_id, workflow.action_name);
        } else {
            actions.push_back(action_inst);
        }
    }
    AttachDescendants(task_, actions);
    LOG_INFO("[{}] Have Tasks:{}", task_name_, strategy.workflow.size());
}

LinkAgeTask::~LinkAgeTask() {
    LOG_INFO("[{}] LinkAgeTask Delete", task_name_);
}

void LinkAgeTask::AttachDescendants(LinkAgeTaskUnit& unit, std::vector<LinkAgeBasePtr>& actions) {
    if (!unit.task) {
        return;
    }

    const auto flow_action_id = unit.task->GetFlowActionId();
    auto it                   = actions.begin();
    while (it != actions.end()) {
        if ((*it)->GetPreFlowActionId() == flow_action_id) {
            LinkAgeTaskUnit child;
            child.task = *it;
            unit.sons.push_back(std::move(child));
            it = actions.erase(it);
        } else {
            ++it;
        }
    }
    if (actions.empty()) {
        return;
    }

    for (auto& son : unit.sons) {
        AttachDescendants(son, actions);
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

bool LinkAgeTask::IsAudioDeviceInUse(const std::string& dev_id) {
    return AnyActionMatches(task_,
                            [&](const LinkAgeBase& action) { return action.IsAudioDeviceInUse(dev_id); });
}

bool LinkAgeTask::IsAudioFileInUse(const std::string& dev_id) {
    return AnyActionMatches(task_,
                            [&](const LinkAgeBase& action) { return action.IsAudioFileInUse(dev_id); });
}
}  // namespace cosmo::linkage
