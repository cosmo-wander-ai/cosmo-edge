// SystemConfigTypes — Data structures for system configuration (extracted from CfgSystemParam.h).

#include "SystemConfigTypes.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {

void to_json(nlohmann::json& j, const RunMode& v) {
    j = static_cast<int>(v);
}
void from_json(const nlohmann::json& j, RunMode& v) {
    v = static_cast<RunMode>(j.get<int>());
}

void from_json(const nlohmann::json& j, CfgSystemDebugInfo& v) {
    JSON_OPT(j, v, bDebugOpen);
    JSON_OPT(j, v, shieldActions);
}

void to_json(nlohmann::json& j, const CfgSystemDebugInfo& v) {
    j["bDebugOpen"]    = v.bDebugOpen;
    j["shieldActions"] = v.shieldActions;
}

void from_json(const nlohmann::json& j, CfgSystemLogoInfo& v) {
    JSON_OPT(j, v, bigScreenName);
    JSON_OPT(j, v, systemName);
    JSON_OPT(j, v, logoFileName);
    JSON_OPT(j, v, bHaveSetLogo);
}

void to_json(nlohmann::json& j, const CfgSystemLogoInfo& v) {
    j["bigScreenName"] = v.bigScreenName;
    j["systemName"]    = v.systemName;
    j["logoFileName"]  = v.logoFileName;
    j["bHaveSetLogo"]  = v.bHaveSetLogo;
}

void from_json(const nlohmann::json& j, CfgSystemPopUpInfo& v) {
    JSON_OPT(j, v, popUpSwitch);
    JSON_OPT(j, v, audioPlay);
    JSON_OPT(j, v, popUpDuration);
}

void to_json(nlohmann::json& j, const CfgSystemPopUpInfo& v) {
    j["popUpSwitch"]   = v.popUpSwitch;
    j["audioPlay"]     = v.audioPlay;
    j["popUpDuration"] = v.popUpDuration;
}

void from_json(const nlohmann::json& j, CfgSystemResourceInfo& v) {
    JSON_OPT(j, v, bResourceLimitOpen);
}

void to_json(nlohmann::json& j, const CfgSystemResourceInfo& v) {
    j["bResourceLimitOpen"] = v.bResourceLimitOpen;
}

void from_json(const nlohmann::json& j, MqttUnitParam& v) {
    JSON_OPT(j, v, bEnable);
    JSON_OPT(j, v, ip);
    JSON_OPT(j, v, port);
    JSON_OPT(j, v, authMode);
    JSON_OPT(j, v, clientId);
    JSON_OPT(j, v, userName);
    JSON_OPT(j, v, passwd);
}

void to_json(nlohmann::json& j, const MqttUnitParam& v) {
    j["bEnable"]  = v.bEnable;
    j["ip"]       = v.ip;
    j["port"]     = v.port;
    j["authMode"] = v.authMode;
    j["clientId"] = v.clientId;
    j["userName"] = v.userName;
    j["passwd"]   = v.passwd;
}

void from_json(const nlohmann::json& j, MqttHttpParam& v) {
    JSON_OPT(j, v, bEnable);
    JSON_OPT(j, v, url);
    JSON_OPT(j, v, ip);
    JSON_OPT(j, v, port);
}

void to_json(nlohmann::json& j, const MqttHttpParam& v) {
    j["bEnable"] = v.bEnable;
    j["url"]     = v.url;
    j["ip"]      = v.ip;
    j["port"]    = v.port;
}

void from_json(const nlohmann::json& j, CfgSystemInterfaceModeIotInfo& v) {
    JSON_OPT(j, v, mqttParam);
    JSON_OPT(j, v, httpParam);
}

void to_json(nlohmann::json& j, const CfgSystemInterfaceModeIotInfo& v) {
    j["mqttParam"] = v.mqttParam;
    j["httpParam"] = v.httpParam;
}

void from_json(const nlohmann::json& j, CfgSystemInterfaceModeInfo& v) {
    JSON_OPT(j, v, runMode);
    JSON_OPT(j, v, iotParam);
    JSON_OPT(j, v, standAloneParam);
}

void to_json(nlohmann::json& j, const CfgSystemInterfaceModeInfo& v) {
    j["runMode"]         = v.runMode;
    j["iotParam"]        = v.iotParam;
    j["standAloneParam"] = v.standAloneParam;
}

void from_json(const nlohmann::json& j, CfgSystemParamInfo& v) {
    JSON_OPT(j, v, logoInfo);
    JSON_OPT(j, v, popUpInfo);
    JSON_OPT(j, v, resourceInfo);
    JSON_OPT(j, v, interfaceModeInfo);
}

void to_json(nlohmann::json& j, const CfgSystemParamInfo& v) {
    j["logoInfo"]          = v.logoInfo;
    j["popUpInfo"]         = v.popUpInfo;
    j["resourceInfo"]      = v.resourceInfo;
    j["interfaceModeInfo"] = v.interfaceModeInfo;
}

void from_json(const nlohmann::json& j, CfgSystemTmpParam& v) {
    JSON_OPT(j, v, debugInfo);
}

void to_json(nlohmann::json& j, const CfgSystemTmpParam& v) {
    j["debugInfo"] = v.debugInfo;
}

}  // namespace cosmo
