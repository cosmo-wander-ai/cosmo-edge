#pragma once

#include "linkage/AlarmOutputController.h"
#include "linkage/LinkAgeBase.h"

namespace cosmo::linkage {

struct LinkAgeAlarmOutputParam {
    int channel{0};
    int duration{0};
};

bool ParseAlarmOutputParam(const LinkAgeParamNode& action, LinkAgeAlarmOutputParam& result);

class LinkAgeAlarmOutput : public LinkAgeBase {
public:
    LinkAgeAlarmOutput(LinkAgeParamNode& action, std::shared_ptr<AlarmOutputController> outputs);
    bool DoAlarm(const std::string& channel_id, const std::string& alg_id) override;

private:
    LinkAgeAlarmOutputParam param_;
    bool valid_{false};
    std::shared_ptr<AlarmOutputController> outputs_;
};

}  // namespace cosmo::linkage
