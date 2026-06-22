#pragma once

#include "linkage/LinkAgeBase.h"
#include "linkage/LinkAgeBaseCommon.h"

namespace cosmo::linkage {

struct LinkAgeTaskUnit {
    LinkAgeBasePtr task{nullptr};
    std::vector<LinkAgeTaskUnit> sons;
};

class LinkAgeTask {
public:
    LinkAgeTask(const std::string& name, LinkageStrategyWorkflow& strategy);
    ~LinkAgeTask();

    void DoAlarm(const std::string& channel_id, const std::string& alg_id);
    bool IsAudioDeviceInUse(const std::string& dev_id);
    bool IsAudioFileInUse(const std::string& dev_id);

private:
    std::vector<LinkAgeBasePtr> FindPres(std::vector<LinkAgeBasePtr>& actions,
                                         const std::string& flow_action_id);
    void AddSons(LinkAgeTaskUnit& unit, const std::vector<LinkAgeBasePtr>& sons);
    void OrganizeTasks(LinkAgeTaskUnit& unit, std::vector<LinkAgeBasePtr>& actions);

    void DoTaskAlarm(LinkAgeTaskUnit& task_unit, const std::string& channel_id, const std::string& alg_id);
    bool IsAudioDeviceInUse(const LinkAgeTaskUnit& task_unit, const std::string& dev_id) const;
    bool IsAudioFileInUse(const LinkAgeTaskUnit& task_unit, const std::string& dev_id) const;

    LinkAgeTaskUnit task_;
    std::string task_name_;
};

using LinkAgeTaskPtr = std::shared_ptr<LinkAgeTask>;
}  // namespace cosmo::linkage
