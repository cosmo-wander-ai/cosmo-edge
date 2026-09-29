// DeviceDiscoveryTypes — DTO types for device discovery multicast protocol.

#include "DeviceDiscoveryTypes.h"

#include <nlohmann/json.hpp>

// Auto-generated JSON serialization
namespace cosmo::service {
void to_json(nlohmann::json& j, const DiscoveryVagueMsg& v) {
    to_json(j, static_cast<const DiscoveryBaseMsg&>(v));
    j["deviceSn"] = v.deviceSn;
}

void from_json(const nlohmann::json& j, DiscoveryVagueMsg& v) {
    from_json(j, static_cast<DiscoveryBaseMsg&>(v));
    if (j.contains("deviceSn") && !j["deviceSn"].is_null())
        j.at("deviceSn").get_to(v.deviceSn);
}

void to_json(nlohmann::json& j, const DiscoveryProbeSend& v) {
    to_json(j, static_cast<const DiscoveryBaseMsg&>(v));
    to_json(j, static_cast<const DiscoverySendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, DiscoveryProbeSend& v) {
    from_json(j, static_cast<DiscoveryBaseMsg&>(v));
    from_json(j, static_cast<DiscoverySendHead&>(v));
    if (j.contains("resData") && !j["resData"].is_null())
        j.at("resData").get_to(v.resData);
}

void from_json(const nlohmann::json& j, DiscoveryBaseMsg& v) {
    if (j.contains("cmd") && !j["cmd"].is_null())
        j.at("cmd").get_to(v.cmd);
    if (j.contains("type") && !j["type"].is_null())
        j.at("type").get_to(v.type);
    if (j.contains("reqId") && !j["reqId"].is_null())
        j.at("reqId").get_to(v.reqId);
}

void to_json(nlohmann::json& j, const DiscoveryBaseMsg& v) {
    j["cmd"]   = v.cmd;
    j["type"]  = v.type;
    j["reqId"] = v.reqId;
}

void from_json(const nlohmann::json& j, DiscoverySendHead& v) {
    if (j.contains("resCode") && !j["resCode"].is_null())
        j.at("resCode").get_to(v.resCode);
    if (j.contains("resMsg") && !j["resMsg"].is_null())
        j.at("resMsg").get_to(v.resMsg);
}

void to_json(nlohmann::json& j, const DiscoverySendHead& v) {
    j["resCode"] = v.resCode;
    j["resMsg"]  = v.resMsg;
}

void from_json(const nlohmann::json& j, DiscoveryProbeSend::ResData& v) {
    if (j.contains("netCardList") && !j["netCardList"].is_null())
        j.at("netCardList").get_to(v.netCardList);
    if (j.contains("devInfoList") && !j["devInfoList"].is_null())
        j.at("devInfoList").get_to(v.devInfoList);
}

void to_json(nlohmann::json& j, const DiscoveryProbeSend::ResData& v) {
    j["netCardList"] = v.netCardList;
    j["devInfoList"] = v.devInfoList;
}

}  // namespace cosmo::service
