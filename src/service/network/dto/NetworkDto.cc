// NetworkDto — Network DTO definitions (extracted from MessageNetworkHandler.h)

#include "NetworkDto.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo::Network {
void to_json(nlohmann::json& j, const MsgQueryNetCardSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryNetCardSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgModifyNetCardRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["netCard"] = v.netCard;
}

void from_json(const nlohmann::json& j, MsgModifyNetCardRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, netCard);
}

void to_json(nlohmann::json& j, const MsgQueryNetDnsSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryNetDnsSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgModifyNetDnsRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["dns1"] = v.dns1;
    j["dns2"] = v.dns2;
}

void from_json(const nlohmann::json& j, MsgModifyNetDnsRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, dns1);
    JSON_OPT(j, v, dns2);
}

void to_json(nlohmann::json& j, const MsgNetworkQualityCheckRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["ip"]         = v.ip;
    j["packetSize"] = v.packetSize;
}

void from_json(const nlohmann::json& j, MsgNetworkQualityCheckRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, ip);
    JSON_OPT(j, v, packetSize);
}

void to_json(nlohmann::json& j, const MsgNetworkQualityCheckSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgNetworkQualityCheckSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgIpAccessibleCheckRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["ip"] = v.ip;
}

void from_json(const nlohmann::json& j, MsgIpAccessibleCheckRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("ip").get_to(v.ip);
}

void to_json(nlohmann::json& j, const MsgIpAccessibleCheckSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgIpAccessibleCheckSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void from_json(const nlohmann::json& j, MsgNetCardInfo& v) {
    JSON_OPT(j, v, mainCard);
    JSON_OPT(j, v, dhcp);
    JSON_OPT(j, v, ethName);
    JSON_OPT(j, v, ipAddr);
    JSON_OPT(j, v, netMask);
    JSON_OPT(j, v, gateway);
    JSON_OPT(j, v, mac);
}

void to_json(nlohmann::json& j, const MsgNetCardInfo& v) {
    j["mainCard"] = v.mainCard;
    j["dhcp"]     = v.dhcp;
    j["ethName"]  = v.ethName;
    j["ipAddr"]   = v.ipAddr;
    j["netMask"]  = v.netMask;
    j["gateway"]  = v.gateway;
    j["mac"]      = v.mac;
}

void from_json(const nlohmann::json& j, MsgQueryNetCardSend::ResData& v) {
    JSON_OPT(j, v, netCardList);
    JSON_OPT(j, v, dns1);
    JSON_OPT(j, v, dns2);
}

void to_json(nlohmann::json& j, const MsgQueryNetCardSend::ResData& v) {
    j["netCardList"] = v.netCardList;
    j["dns1"]        = v.dns1;
    j["dns2"]        = v.dns2;
}

void from_json(const nlohmann::json& j, MsgQueryNetDnsSend::ResData& v) {
    JSON_OPT(j, v, dns1);
    JSON_OPT(j, v, dns2);
}

void to_json(nlohmann::json& j, const MsgQueryNetDnsSend::ResData& v) {
    j["dns1"] = v.dns1;
    j["dns2"] = v.dns2;
}

void from_json(const nlohmann::json& j, MsgNetworkQualityCheckSend::ResData& v) {
    JSON_OPT(j, v, lostRate);
    JSON_OPT(j, v, averageLatency);
}

void to_json(nlohmann::json& j, const MsgNetworkQualityCheckSend::ResData& v) {
    j["lostRate"]       = v.lostRate;
    j["averageLatency"] = v.averageLatency;
}

void from_json(const nlohmann::json& j, MsgIpAccessibleCheckSend::ResData& v) {
    JSON_OPT(j, v, ip);
    JSON_OPT(j, v, accessible);
}

void to_json(nlohmann::json& j, const MsgIpAccessibleCheckSend::ResData& v) {
    j["ip"]         = v.ip;
    j["accessible"] = v.accessible;
}

}  // namespace cosmo::Network
