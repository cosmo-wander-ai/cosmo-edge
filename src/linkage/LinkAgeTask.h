#pragma once

#include "linkage/LinkAgeBase.h"
#include "linkage/LinkAgeBaseCommon.h"

namespace cosmo::linkage {

class AlarmOutputController;

struct LinkAgeTaskUnit {
    LinkAgeBasePtr task{nullptr};
    std::vector<LinkAgeTaskUnit> sons;
};

class LinkAgeTask {
public:
    LinkAgeTask(const std::string& name, LinkageStrategyWorkflow& strategy,
                std::shared_ptr<AlarmOutputController> outputs = nullptr);
    ~LinkAgeTask();

    void DoAlarm(const std::string& channel_id, const std::string& alg_id);
    bool IsAudioDeviceInUse(const std::string& dev_id);
    bool IsAudioFileInUse(const std::string& dev_id);

private:
    void AttachDescendants(LinkAgeTaskUnit& unit, std::vector<LinkAgeBasePtr>& actions);

    void DoTaskAlarm(LinkAgeTaskUnit& task_unit, const std::string& channel_id, const std::string& alg_id);

    LinkAgeTaskUnit task_;
    std::string task_name_;
};

using LinkAgeTaskPtr = std::shared_ptr<LinkAgeTask>;
}  // namespace cosmo::linkage
