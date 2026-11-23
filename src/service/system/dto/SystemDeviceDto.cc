// SystemDeviceDto — Query device info status request

#include "SystemDeviceDto.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo::System {
void to_json(nlohmann::json& j, const MsgQueryDeviceInfoSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryDeviceInfoSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgQueryHardwareResourceSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryHardwareResourceSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgQueryDeviceStatusSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryDeviceStatusSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void from_json(const nlohmann::json& j, MsgQueryDeviceInfoSend::ResData& v) {
    JSON_OPT(j, v, devInfoList);
}

void to_json(nlohmann::json& j, const MsgQueryDeviceInfoSend::ResData& v) {
    j["devInfoList"] = v.devInfoList;
}

void from_json(const nlohmann::json& j, MsgQueryHardwareResourceSend::Item& v) {
    JSON_OPT(j, v, key);
    JSON_OPT(j, v, name);
    JSON_OPT(j, v, usedPercent);
    JSON_OPT(j, v, usedSize);
    JSON_OPT(j, v, unusedSize);
    JSON_OPT(j, v, available);
    JSON_OPT(j, v, memoryDomain);
    JSON_OPT(j, v, totalSize);
    JSON_OPT(j, v, allocatedSize);
}

void to_json(nlohmann::json& j, const MsgQueryHardwareResourceSend::Item& v) {
    j["key"]          = v.key;
    j["name"]         = v.name;
    j["usedPercent"]  = v.usedPercent;
    j["usedSize"]     = v.usedSize;
    j["unusedSize"]   = v.unusedSize;
    j["available"]    = v.available;
    j["memoryDomain"] = v.memoryDomain;
    if (!v.totalSize.empty())
        j["totalSize"] = v.totalSize;
    if (!v.allocatedSize.empty())
        j["allocatedSize"] = v.allocatedSize;
}

void from_json(const nlohmann::json& j, MsgQueryHardwareResourceSend::ResData& v) {
    JSON_OPT(j, v, customScore);
    JSON_OPT(j, v, itemList);
    JSON_OPT(j, v, accelerator);
}

void to_json(nlohmann::json& j, const MsgQueryHardwareResourceSend::ResData& v) {
    j["customScore"] = v.customScore;
    j["itemList"]    = v.itemList;
    j["accelerator"] = v.accelerator;
}

void from_json(const nlohmann::json& j, MsgQueryDeviceStatusSend::ResData& v) {
    JSON_OPT(j, v, bootId);
    JSON_OPT(j, v, softwareVersion);
}

void to_json(nlohmann::json& j, const MsgQueryDeviceStatusSend::ResData& v) {
    j["bootId"]          = v.bootId;
    j["softwareVersion"] = v.softwareVersion;
}

}  // namespace cosmo::System
