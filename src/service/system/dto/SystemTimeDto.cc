// SystemTimeDto — Time fetch request

#include "SystemTimeDto.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo::System {
void to_json(nlohmann::json& j, const MsgQueryTimeSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryTimeSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgNTPDateRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["ntpEnable"]  = v.ntpEnable;
    j["ntpServer"]  = v.ntpServer;
    j["ntpPort"]    = v.ntpPort;
    j["interval"]   = v.interval;
    j["timeZoneId"] = v.timeZoneId;
}

void from_json(const nlohmann::json& j, MsgNTPDateRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, ntpEnable);
    JSON_OPT(j, v, ntpServer);
    JSON_OPT(j, v, ntpPort);
    JSON_OPT(j, v, interval);
    JSON_OPT(j, v, timeZoneId);
}

void to_json(nlohmann::json& j, const MsgNTPDateSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgNTPDateSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgModifyTimeRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["timestamp"]  = v.timestamp;
    j["timeZoneId"] = v.timeZoneId;
}

void from_json(const nlohmann::json& j, MsgModifyTimeRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("timestamp").get_to(v.timestamp);
    j.at("timeZoneId").get_to(v.timeZoneId);
}

void from_json(const nlohmann::json& j, MsgQueryTimeSend::ZoneInfo& v) {
    JSON_OPT(j, v, name);
    JSON_OPT(j, v, value);
    JSON_OPT(j, v, id);
}

void to_json(nlohmann::json& j, const MsgQueryTimeSend::ZoneInfo& v) {
    j["name"]  = v.name;
    j["value"] = v.value;
    j["id"]    = v.id;
}

void from_json(const nlohmann::json& j, MsgQueryTimeSend::Ntp& v) {
    JSON_OPT(j, v, enable);
    JSON_OPT(j, v, server);
    JSON_OPT(j, v, interval);
    JSON_OPT(j, v, port);
}

void to_json(nlohmann::json& j, const MsgQueryTimeSend::Ntp& v) {
    j["enable"]   = v.enable;
    j["server"]   = v.server;
    j["interval"] = v.interval;
    j["port"]     = v.port;
}

void from_json(const nlohmann::json& j, MsgQueryTimeSend::TimeStatus& v) {
    JSON_OPT(j, v, timestamp);
    JSON_OPT(j, v, timeString);
    JSON_OPT(j, v, timeZoneValue);
    JSON_OPT(j, v, timeZoneId);
    JSON_OPT(j, v, ntp);
}

void to_json(nlohmann::json& j, const MsgQueryTimeSend::TimeStatus& v) {
    j["timestamp"]     = v.timestamp;
    j["timeString"]    = v.timeString;
    j["timeZoneValue"] = v.timeZoneValue;
    j["timeZoneId"]    = v.timeZoneId;
    j["ntp"]           = v.ntp;
}

void from_json(const nlohmann::json& j, MsgQueryTimeSend::ResData& v) {
    JSON_OPT(j, v, timeStatus);
    JSON_OPT(j, v, zoneInfoList);
}

void to_json(nlohmann::json& j, const MsgQueryTimeSend::ResData& v) {
    j["timeStatus"]   = v.timeStatus;
    j["zoneInfoList"] = v.zoneInfoList;
}

void from_json(const nlohmann::json& j, MsgNTPDateSend::ResData& v) {
    JSON_OPT(j, v, status);
    JSON_OPT(j, v, statusMsg);
}

void to_json(nlohmann::json& j, const MsgNTPDateSend::ResData& v) {
    j["status"]    = v.status;
    j["statusMsg"] = v.statusMsg;
}

}  // namespace cosmo::System
