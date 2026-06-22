// InfraMsgTypes — Infrastructure msg types — MsgNetCardInfo, StorageList

#include "InfraMsgTypes.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void from_json(const nlohmann::json& j, MsgNetCardInfo& v) {
    JSON_OPT(j, v, mainCard);
    JSON_OPT(j, v, dhcp);
    JSON_OPT(j, v, ethName);
    JSON_OPT(j, v, ipAddr);
    JSON_OPT(j, v, netMask);
    JSON_OPT(j, v, gateway);
    JSON_OPT(j, v, mac);
    JSON_OPT(j, v, dns1);
    JSON_OPT(j, v, dns2);
}

void to_json(nlohmann::json& j, const MsgNetCardInfo& v) {
    j["mainCard"] = v.mainCard;
    j["dhcp"]     = v.dhcp;
    j["ethName"]  = v.ethName;
    j["ipAddr"]   = v.ipAddr;
    j["netMask"]  = v.netMask;
    j["gateway"]  = v.gateway;
    j["mac"]      = v.mac;
    j["dns1"]     = v.dns1;
    j["dns2"]     = v.dns2;
}

void from_json(const nlohmann::json& j, StorageList& v) {
    JSON_OPT(j, v, id);
    JSON_OPT(j, v, businessCategory);
    JSON_OPT(j, v, actionName);
    JSON_OPT(j, v, remark);
    JSON_OPT(j, v, inputParamConfig);
}

void to_json(nlohmann::json& j, const StorageList& v) {
    j["id"]               = v.id;
    j["businessCategory"] = v.businessCategory;
    j["actionName"]       = v.actionName;
    j["remark"]           = v.remark;
    j["inputParamConfig"] = v.inputParamConfig;
}

}  // namespace cosmo
