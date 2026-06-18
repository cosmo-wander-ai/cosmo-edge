// Audio domain data types — extracted from device/AudioMng.h and AudioDeviceMng.h
// to break compile-time coupling between service interfaces and device internals.

#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace cosmo {

struct AlarmAudioInfo {
    std::string name;      // (Display name)
    std::string id;        // (File ID) UUID generated on save
    std::string fileName;  // (File name) UUID generated on save
    int64_t timestamp{0};  // Timestamp (ms)
    friend void to_json(nlohmann::json& j, const AlarmAudioInfo& v);
    friend void from_json(const nlohmann::json& j, AlarmAudioInfo& v);
};

enum class AudioDevicePlayType {
    AudioDevicePlayTypeNone      = 0,
    AudioDevicePlayTypeAudioPlay = 1,
    AudioDevicePlayTypeTextPlay  = 2,
};

enum class AudioDeviceVoiceTone { AudioDeviceVoiceToneMale = 0, AudioDeviceVoiceToneFemale = 1 };

struct AudioDeviceInfo {
    std::string name;  // (Display name)
    std::string id;    // (File ID) UUID generated on save
    std::string ip;    // IP address
    std::string ethName{"eth0"};
    int64_t timestamp{0};  // Timestamp (ms)
    friend void to_json(nlohmann::json& j, const AudioDeviceInfo& v);
    friend void from_json(const nlohmann::json& j, AudioDeviceInfo& v);
};

struct AudioDevicePlay {
    std::string devId;
    AudioDevicePlayType playType{AudioDevicePlayType::AudioDevicePlayTypeNone};
    std::string data;
    AudioDeviceVoiceTone tone{AudioDeviceVoiceTone::AudioDeviceVoiceToneMale};
    int volume{50};
    int speed{50};
    int duration{600};
    int times{1};
    int gap{2};
};

}  // namespace cosmo
