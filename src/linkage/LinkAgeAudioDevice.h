#pragma once

#include "linkage/LinkAgeBase.h"
#include "linkage/LinkAgeBaseCommon.h"

namespace cosmo::linkage {
enum class LinkAgeAudioDeviceOperation {
    kNone      = 0,
    kAudioPlay = 1,
    kTextPlay  = 2,
};
constexpr auto kAudioDeviceOperationDefault = LinkAgeAudioDeviceOperation::kAudioPlay;
constexpr bool IsValidOperation(int value) {
    return static_cast<LinkAgeAudioDeviceOperation>(value) >= LinkAgeAudioDeviceOperation::kAudioPlay &&
           static_cast<LinkAgeAudioDeviceOperation>(value) <= LinkAgeAudioDeviceOperation::kTextPlay;
}
enum class LinkAgeAudioDeviceTone {
    kMale   = 0,
    kFemale = 1,
};
constexpr bool IsValidTone(int value) {
    return static_cast<LinkAgeAudioDeviceTone>(value) >= LinkAgeAudioDeviceTone::kMale &&
           static_cast<LinkAgeAudioDeviceTone>(value) <= LinkAgeAudioDeviceTone::kFemale;
}
constexpr auto kAudioDeviceToneDefault = LinkAgeAudioDeviceTone::kMale;
constexpr auto kAudioDeviceToneMin     = LinkAgeAudioDeviceTone::kMale;
constexpr auto kAudioDeviceToneMax     = LinkAgeAudioDeviceTone::kFemale;

constexpr int kAudioDeviceVolumeDefault = 50;
constexpr int kAudioDeviceVolumeMin     = 1;
constexpr int kAudioDeviceVolumeMax     = 100;
constexpr bool IsValidVolume(int value) {
    return value >= kAudioDeviceVolumeMin && value <= kAudioDeviceVolumeMax;
}

constexpr int kAudioDeviceSpeedDefault = 50;
constexpr int kAudioDeviceSpeedMin     = 0;
constexpr int kAudioDeviceSpeedMax     = 100;
constexpr bool IsValidSpeed(int value) {
    return value >= kAudioDeviceSpeedMin && value <= kAudioDeviceSpeedMax;
}

constexpr int kAudioDeviceDurationDefault = 60;
constexpr int kAudioDeviceDurationMin     = 60;
constexpr int kAudioDeviceDurationMax     = 600;
constexpr bool IsValidDuration(int value) {
    return value >= kAudioDeviceDurationMin && value <= kAudioDeviceDurationMax;
}

constexpr int kAudioDeviceTimesDefault = 1;
constexpr int kAudioDeviceTimesMin     = 1;
constexpr int kAudioDeviceTimesMax     = 100;
constexpr bool IsValidTimes(int value) {
    return value >= kAudioDeviceTimesMin && value <= kAudioDeviceTimesMax;
}

constexpr int kAudioDeviceGapDefault = 60;
constexpr int kAudioDeviceGapMin     = 60;
constexpr int kAudioDeviceGapMax     = 600;
constexpr bool IsValidGap(int value) {
    return value >= kAudioDeviceGapMin && value <= kAudioDeviceGapMax;
}

struct LinkAgeAudioDeviceParam {
    std::string dev_id;  // Audio column ID
    LinkAgeAudioDeviceOperation operation{LinkAgeAudioDeviceOperation::kNone};
    std::string data;  // Audio file ID or text content
    LinkAgeAudioDeviceTone tone{kAudioDeviceToneDefault};
    int volume{kAudioDeviceVolumeDefault};
    int speed{kAudioDeviceSpeedDefault};
    int duration{kAudioDeviceDurationDefault};
    int times{kAudioDeviceTimesDefault};
    int gap{kAudioDeviceGapDefault};
};

class LinkAgeAudioDevice : public LinkAgeBase {
public:
    explicit LinkAgeAudioDevice(LinkAgeParamNode& action);
    ~LinkAgeAudioDevice() override;

    bool DoAlarm(const std::string& channel_id, const std::string& alg_id) override;
    bool IsAudioDeviceInUse(const std::string& dev_id) const override;
    bool IsAudioFileInUse(const std::string& dev_id) const override;

private:
    LinkAgeAudioDeviceParam param_;
};

using LinkAgeAudioDevicePtr = std::shared_ptr<LinkAgeAudioDevice>;
}  // namespace cosmo::linkage
