#pragma once

#include "linkage/LinkAgeBase.h"
#include "linkage/LinkAgeBaseCommon.h"

namespace cosmo::linkage {
struct LinkAgeAlarmTaskUnit {
    std::string channel_id;
    std::string algorithm_id;
    friend void to_json(nlohmann::json& j, const LinkAgeAlarmTaskUnit& v);
    friend void from_json(const nlohmann::json& j, LinkAgeAlarmTaskUnit& v);
};
struct LinkAgeAlarmTaskParam {
    std::vector<LinkAgeAlarmTaskUnit> tasks;
};

class LinkAgeAlarm : public LinkAgeBase {
public:
    explicit LinkAgeAlarm(LinkAgeParamNode& action);
    ~LinkAgeAlarm();
    bool DoAlarm(const std::string& channel_id, const std::string& alg_id) override;

private:
    void AnalysisTasks(const std::string& value);

    LinkAgeAlarmTaskParam param_;
};

using LinkAgeAlarmPtr = std::shared_ptr<LinkAgeAlarm>;
}  // namespace cosmo::linkage
