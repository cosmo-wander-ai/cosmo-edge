#include "linkage/LinkAgeAlarmOutput.h"

#include <charconv>
#include <utility>

namespace cosmo::linkage {

bool ParseAlarmOutputParam(const LinkAgeParamNode& action, LinkAgeAlarmOutputParam& result) {
    LinkAgeAlarmOutputParam parsed;
    bool has_channel  = false;
    bool has_duration = false;
    for (const auto& param : action.config_object.params) {
        const auto key          = param.key.ToString();
        const auto value        = param.value.ToString();
        int number              = 0;
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
        if (error != std::errc{} || end != value.data() + value.size()) {
            return false;
        }
        if (key == "outputChannel" && !has_channel && number >= 1 && number <= 64) {
            parsed.channel = number;
            has_channel    = true;
        } else if (key == "duration" && !has_duration && number >= 1 && number <= 600) {
            parsed.duration = number;
            has_duration    = true;
        } else {
            return false;
        }
    }
    if (!has_channel || !has_duration) {
        return false;
    }
    result = parsed;
    return true;
}

LinkAgeAlarmOutput::LinkAgeAlarmOutput(LinkAgeParamNode& action,
                                       std::shared_ptr<AlarmOutputController> outputs)
    : LinkAgeBase(action), valid_(ParseAlarmOutputParam(action, param_)), outputs_(std::move(outputs)) {}

bool LinkAgeAlarmOutput::DoAlarm(const std::string& /*channel_id*/, const std::string& /*alg_id*/) {
    return valid_ && outputs_ && outputs_->Pulse(param_.channel, std::chrono::seconds(param_.duration));
}

}  // namespace cosmo::linkage
