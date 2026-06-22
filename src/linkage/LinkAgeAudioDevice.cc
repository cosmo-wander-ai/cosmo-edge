// LinkAgeAudioDevice — Link Age Audio Device implementation.

#include "linkage/LinkAgeAudioDevice.h"

#include "service/detail/ServiceRegistry.h"
#include "service/media/IAudioService.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/SafeParse.h"

namespace cosmo::linkage {
LinkAgeAudioDevice::LinkAgeAudioDevice(LinkAgeParamNode& action) : LinkAgeBase(action) {
    for (const auto& param : action.config_object.params) {
        if (kKeyStrageAudioDeviceId == param.key.ToString()) {
            param_.dev_id = param.value;
        } else if (kKeyStrageAudioDeviceOperation == param.key.ToString()) {
            auto value = util::ParseInt(param.value);
            if (IsValidOperation(value)) {
                param_.operation = static_cast<LinkAgeAudioDeviceOperation>(value);
            } else {
                LOG_WARN("{} Set {} To {} Invalid", GetFlowActionId(), param.key, param.value);
            }
        } else if (kKeyStrageAudioDeviceData == param.key.ToString()) {
            param_.data = param.value;
        } else if (kKeyStrageAudioDeviceText == param.key.ToString()) {
            param_.data = param.value;
        } else if (kKeyStrageAudioDeviceTone == param.key.ToString()) {
            auto value = util::ParseInt(param.value);
            if (IsValidTone(value)) {
                param_.tone = static_cast<LinkAgeAudioDeviceTone>(value);
            } else {
                LOG_WARN("{} Set {} To {} Invalid", GetFlowActionId(), param.key, param.value);
            }
        } else if (kKeyStrageAudioDeviceSpeed == param.key.ToString()) {
            auto value = util::ParseInt(param.value);
            if (IsValidSpeed(value)) {
                param_.speed = value;
            } else {
                LOG_WARN("{} Set {} To {} Invalid", GetFlowActionId(), param.key, param.value);
            }
        } else if (kKeyStrageAudioDeviceVolume == param.key.ToString()) {
            auto value = util::ParseInt(param.value);
            if (IsValidVolume(value)) {
                param_.volume = value;
            } else {
                LOG_WARN("{} Set {} To {} Invalid", GetFlowActionId(), param.key, param.value);
            }
        } else if (kKeyStrageAudioDeviceDuration == param.key.ToString()) {
            auto value = util::ParseInt(param.value);
            if (IsValidDuration(value)) {
                param_.duration = value;
            } else {
                LOG_WARN("{} Set {} To {} Invalid", GetFlowActionId(), param.key, param.value);
            }
        } else if (kKeyStrageAudioDeviceTimes == param.key.ToString()) {
            auto value = util::ParseInt(param.value);
            if (IsValidTimes(value)) {
                param_.times = value;
            } else {
                LOG_WARN("{} Set {} To {} Invalid", GetFlowActionId(), param.key, param.value);
            }
        } else if (kKeyStrageAudioDeviceGap == param.key.ToString()) {
            auto value = util::ParseInt(param.value);
            if (IsValidGap(value)) {
                param_.gap = value;
            } else {
                LOG_WARN("{} Set {} To {} Invalid", GetFlowActionId(), param.key, param.value);
            }
        }
    }

    LOG_INFO(
        "{} Param: AudioDev:{} Op:{} data:{} Duration:{} Speed:{} Times:{} tone:{} volume:{} Interval:{}",
        GetFlowActionId(), param_.dev_id, param_.operation, param_.data, param_.duration, param_.speed,
        param_.times, param_.tone, param_.volume, param_.gap);
}

LinkAgeAudioDevice::~LinkAgeAudioDevice() {
    LOG_INFO("{}", "LinkAgeAudioDevice Delete");
}

bool LinkAgeAudioDevice::DoAlarm(const std::string& /*channel_id*/, const std::string& /*alg_id*/) {
    if (param_.dev_id.empty()) {
        LOG_WARN("{}", "Invalid Devid");
        return false;
    }
    if (param_.data.empty()) {
        LOG_WARN("{}", "Invalid Data");
        return false;
    }
    AudioDevicePlay play_info;
    if (param_.operation == LinkAgeAudioDeviceOperation::kAudioPlay) {
        play_info.playType = AudioDevicePlayType::AudioDevicePlayTypeAudioPlay;
    } else if (param_.operation == LinkAgeAudioDeviceOperation::kTextPlay) {
        play_info.playType = AudioDevicePlayType::AudioDevicePlayTypeTextPlay;
    } else {
        LOG_WARN("{}", "Invalid Operation");
        return false;
    }
    if (param_.tone == LinkAgeAudioDeviceTone::kMale) {
        play_info.tone = AudioDeviceVoiceTone::AudioDeviceVoiceToneMale;
    } else if (param_.tone == LinkAgeAudioDeviceTone::kFemale) {
        play_info.tone = AudioDeviceVoiceTone::AudioDeviceVoiceToneFemale;
    } else {
        LOG_WARN("{}", "Invalid Operation");
        return false;
    }
    play_info.devId    = param_.dev_id;
    play_info.data     = param_.data;
    play_info.volume   = param_.volume;
    play_info.speed    = param_.speed;
    play_info.duration = param_.duration;
    play_info.times    = param_.times;
    play_info.gap      = param_.gap;
    return cosmo::service::ServiceRegistry::Instance().Get<service::IAudioService>().PlayAudioDevice(
        play_info);
}

bool LinkAgeAudioDevice::IsAudioDeviceInUse(const std::string& dev_id) const {
    return (param_.dev_id == dev_id);
}

bool LinkAgeAudioDevice::IsAudioFileInUse(const std::string& dev_id) const {
    if (param_.operation == LinkAgeAudioDeviceOperation::kAudioPlay) {
        return (param_.data == dev_id);
    }
    return false;
}

}  // namespace cosmo::linkage
