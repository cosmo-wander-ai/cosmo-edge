#pragma once

#include "linkage/LinkAgeBaseCommon.h"

namespace cosmo::linkage {

class LinkAgeBase {
public:
    explicit LinkAgeBase(LinkAgeParamNode& action);
    virtual ~LinkAgeBase() = default;

    std::string GetActionId() const {
        return action_.action_id;
    }

    std::string GetFlowActionId() const {
        return action_.flowActionId;
    }

    std::string GetPreFlowActionId() const {
        return action_.preFlowActionId;
    }

    std::string GetName() const {
        return action_.action_name;
    }

    virtual bool DoAlarm(const std::string& channel_id, const std::string& alg_id);

    virtual bool IsAudioDeviceInUse(const std::string& dev_id) const;
    virtual bool IsAudioFileInUse(const std::string& dev_id) const;

private:
    LinkAgeParamNode action_;
};

using LinkAgeBasePtr = std::shared_ptr<LinkAgeBase>;
}  // namespace cosmo::linkage
