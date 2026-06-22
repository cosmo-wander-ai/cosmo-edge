// AudioDeviceDto — Audio domain data types — extracted from device/AudioMng.h and AudioDeviceMng.h

#include "AudioDeviceDto.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void from_json(const nlohmann::json& j, AlarmAudioInfo& v) {
    JSON_OPT(j, v, name);
    JSON_OPT(j, v, id);
    JSON_OPT(j, v, fileName);
    JSON_OPT(j, v, timestamp);
}

void to_json(nlohmann::json& j, const AlarmAudioInfo& v) {
    j["name"]      = v.name;
    j["id"]        = v.id;
    j["fileName"]  = v.fileName;
    j["timestamp"] = v.timestamp;
}

void from_json(const nlohmann::json& j, AudioDeviceInfo& v) {
    JSON_OPT(j, v, name);
    JSON_OPT(j, v, id);
    JSON_OPT(j, v, ip);
    JSON_OPT(j, v, timestamp);
    JSON_OPT(j, v, ethName);
}

void to_json(nlohmann::json& j, const AudioDeviceInfo& v) {
    j["name"]      = v.name;
    j["id"]        = v.id;
    j["ip"]        = v.ip;
    j["timestamp"] = v.timestamp;
    j["ethName"]   = v.ethName;
}

}  // namespace cosmo
