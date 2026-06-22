// SystemNetworkDto — HTTP interface parameters set request

#include "SystemNetworkDto.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo::System {
void to_json(nlohmann::json& j, const MsgSetHttpInterfaceParamRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["url"]    = v.url;
    j["switch"] = v.enable;
}

void from_json(const nlohmann::json& j, MsgSetHttpInterfaceParamRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, url);
    JSON_OPT_KEY(j, v, "switch", enable);
}

void to_json(nlohmann::json& j, const MsgQueryHttpInterfaceParamSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryHttpInterfaceParamSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgSetMqttAdapterParamRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["url"]      = v.url;
    j["port"]     = v.port;
    j["authMode"] = v.authMode;
    j["clientId"] = v.clientId;
    j["userName"] = v.userName;
    j["passwd"]   = v.passwd;
    j["switch"]   = v.enable;
}

void from_json(const nlohmann::json& j, MsgSetMqttAdapterParamRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, url);
    JSON_OPT(j, v, port);
    JSON_OPT(j, v, authMode);
    JSON_OPT(j, v, clientId);
    JSON_OPT(j, v, userName);
    JSON_OPT(j, v, passwd);
    JSON_OPT_KEY(j, v, "switch", enable);
}

void to_json(nlohmann::json& j, const MsgQueryMqttAdapterParamSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryMqttAdapterParamSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgQueryRunModeParamSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryRunModeParamSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgModifyRunModeParamRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["runMode"] = v.runMode;
}

void from_json(const nlohmann::json& j, MsgModifyRunModeParamRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, runMode);
}

void to_json(nlohmann::json& j, const MsgQueryIotNetworkParamSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryIotNetworkParamSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgModifyIotNetworkParamRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    to_json(j, static_cast<const MsgIotNetworkParam&>(v));
}

void from_json(const nlohmann::json& j, MsgModifyIotNetworkParamRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    from_json(j, static_cast<MsgIotNetworkParam&>(v));
}

void from_json(const nlohmann::json& j, HttpInterfaceParamData& v) {
    JSON_OPT(j, v, enable);
    JSON_OPT_KEY(j, v, "switch", enable);
    JSON_OPT(j, v, url);
}

void to_json(nlohmann::json& j, const HttpInterfaceParamData& v) {
    j["enable"] = v.enable;
    j["switch"] = v.enable;
    j["url"]    = v.url;
}

void from_json(const nlohmann::json& j, MqttAdapterParamData& v) {
    JSON_OPT(j, v, enable);
    JSON_OPT_KEY(j, v, "switch", enable);
    JSON_OPT(j, v, url);
    JSON_OPT(j, v, port);
    JSON_OPT(j, v, status);
    JSON_OPT(j, v, authMode);
    JSON_OPT(j, v, clientId);
    JSON_OPT(j, v, userName);
    JSON_OPT(j, v, passwd);
}

void to_json(nlohmann::json& j, const MqttAdapterParamData& v) {
    j["enable"]   = v.enable;
    j["switch"]   = v.enable;
    j["url"]      = v.url;
    j["port"]     = v.port;
    j["status"]   = v.status;
    j["authMode"] = v.authMode;
    j["clientId"] = v.clientId;
    j["userName"] = v.userName;
    j["passwd"]   = v.passwd;
}

void from_json(const nlohmann::json& j, RunModeParamData& v) {
    JSON_OPT(j, v, runMode);
}

void to_json(nlohmann::json& j, const RunModeParamData& v) {
    j["runMode"] = v.runMode;
}

void from_json(const nlohmann::json& j, MsgIotNetworkParam& v) {
    JSON_OPT(j, v, mqttIp);
    JSON_OPT(j, v, mqttPort);
    JSON_OPT(j, v, httpUrl);
    JSON_OPT(j, v, status);
}

void to_json(nlohmann::json& j, const MsgIotNetworkParam& v) {
    j["mqttIp"]   = v.mqttIp;
    j["mqttPort"] = v.mqttPort;
    j["httpUrl"]  = v.httpUrl;
    j["status"]   = v.status;
}

}  // namespace cosmo::System
