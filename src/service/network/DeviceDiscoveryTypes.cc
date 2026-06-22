// DeviceDiscoveryTypes — DTO types for device discovery multicast protocol.

#include "DeviceDiscoveryTypes.h"

#include <nlohmann/json.hpp>

#include "util/LimitedTypeJson.h"

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
    to_json(j, static_cast<const DiscoverySendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, DiscoveryProbeSend& v) {
    from_json(j, static_cast<DiscoverySendHead&>(v));
    if (j.contains("resData") && !j["resData"].is_null())
        j.at("resData").get_to(v.resData);
}

void to_json(nlohmann::json& j, const ModifyNetCardRequest& v) {
    to_json(j, static_cast<const DiscoveryVagueMsg&>(v));
    j["passwd"]  = v.passwd;
    j["netCard"] = v.netCard;
}

void from_json(const nlohmann::json& j, ModifyNetCardRequest& v) {
    from_json(j, static_cast<DiscoveryVagueMsg&>(v));
    if (j.contains("passwd") && !j["passwd"].is_null())
        j.at("passwd").get_to(v.passwd);
    if (j.contains("netCard") && !j["netCard"].is_null())
        j.at("netCard").get_to(v.netCard);
}

void to_json(nlohmann::json& j, const ModifyNetCardResponse& v) {
    to_json(j, static_cast<const DiscoveryBaseMsg&>(v));
    to_json(j, static_cast<const DiscoverySendHead&>(v));
}

void from_json(const nlohmann::json& j, ModifyNetCardResponse& v) {
    from_json(j, static_cast<DiscoveryBaseMsg&>(v));
    from_json(j, static_cast<DiscoverySendHead&>(v));
}

void to_json(nlohmann::json& j, const HWInfoWriteRequest& v) {
    to_json(j, static_cast<const DiscoveryVagueMsg&>(v));
    j["devHWInfo"] = v.devHWInfo;
}

void from_json(const nlohmann::json& j, HWInfoWriteRequest& v) {
    from_json(j, static_cast<DiscoveryVagueMsg&>(v));
    if (j.contains("devHWInfo") && !j["devHWInfo"].is_null())
        j.at("devHWInfo").get_to(v.devHWInfo);
}

void to_json(nlohmann::json& j, const HWInfoWriteResponse& v) {
    to_json(j, static_cast<const DiscoverySendHead&>(v));
    j["devHWInfo"] = v.devHWInfo;
}

void from_json(const nlohmann::json& j, HWInfoWriteResponse& v) {
    from_json(j, static_cast<DiscoverySendHead&>(v));
    if (j.contains("devHWInfo") && !j["devHWInfo"].is_null())
        j.at("devHWInfo").get_to(v.devHWInfo);
}

void to_json(nlohmann::json& j, const AuthCodeInfoSend& v) {
    to_json(j, static_cast<const DiscoverySendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, AuthCodeInfoSend& v) {
    from_json(j, static_cast<DiscoverySendHead&>(v));
    if (j.contains("resData") && !j["resData"].is_null())
        j.at("resData").get_to(v.resData);
}

void to_json(nlohmann::json& j, const AuthCodeModifyRequest& v) {
    to_json(j, static_cast<const DiscoveryVagueMsg&>(v));
    j["authCode"] = v.authCode;
}

void from_json(const nlohmann::json& j, AuthCodeModifyRequest& v) {
    from_json(j, static_cast<DiscoveryVagueMsg&>(v));
    if (j.contains("authCode") && !j["authCode"].is_null())
        j.at("authCode").get_to(v.authCode);
}

void to_json(nlohmann::json& j, const AuthCodeModifyResponse& v) {
    to_json(j, static_cast<const DiscoveryBaseMsg&>(v));
    to_json(j, static_cast<const AuthCodeInfoSend&>(v));
}

void from_json(const nlohmann::json& j, AuthCodeModifyResponse& v) {
    from_json(j, static_cast<DiscoveryBaseMsg&>(v));
    from_json(j, static_cast<AuthCodeInfoSend&>(v));
}

void to_json(nlohmann::json& j, const AuthStatusQueryResponse& v) {
    to_json(j, static_cast<const DiscoveryBaseMsg&>(v));
    to_json(j, static_cast<const AuthCodeInfoSend&>(v));
}

void from_json(const nlohmann::json& j, AuthStatusQueryResponse& v) {
    from_json(j, static_cast<DiscoveryBaseMsg&>(v));
    from_json(j, static_cast<AuthCodeInfoSend&>(v));
}

void to_json(nlohmann::json& j, const LoginHttpRequest& v) {
    to_json(j, static_cast<const cosmo::MsgRecvHead&>(v));
    j["user"]   = v.user;
    j["passwd"] = v.passwd;
}

void from_json(const nlohmann::json& j, LoginHttpRequest& v) {
    from_json(j, static_cast<cosmo::MsgRecvHead&>(v));
    j.at("user").get_to(v.user);
    j.at("passwd").get_to(v.passwd);
}

void to_json(nlohmann::json& j, const LoginHttpResponse& v) {
    to_json(j, static_cast<const DiscoverySendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, LoginHttpResponse& v) {
    from_json(j, static_cast<DiscoverySendHead&>(v));
    if (j.contains("resData") && !j["resData"].is_null())
        j.at("resData").get_to(v.resData);
}

void to_json(nlohmann::json& j, const AuthCodeHttpRequest& v) {
    to_json(j, static_cast<const cosmo::MsgRecvHead&>(v));
    j["authorCode"] = v.authorCode;
}

void from_json(const nlohmann::json& j, AuthCodeHttpRequest& v) {
    from_json(j, static_cast<cosmo::MsgRecvHead&>(v));
    if (j.contains("authorCode") && !j["authorCode"].is_null())
        j.at("authorCode").get_to(v.authorCode);
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

void from_json(const nlohmann::json& j, DeviceHWInfo& v) {
    if (j.contains("devSn") && !j["devSn"].is_null())
        j.at("devSn").get_to(v.devSn);
    if (j.contains("devType") && !j["devType"].is_null())
        j.at("devType").get_to(v.devType);
    if (j.contains("hwVersion") && !j["hwVersion"].is_null())
        j.at("hwVersion").get_to(v.hwVersion);
}

void to_json(nlohmann::json& j, const DeviceHWInfo& v) {
    j["devSn"]     = v.devSn;
    j["devType"]   = v.devType;
    j["hwVersion"] = v.hwVersion;
}

void from_json(const nlohmann::json& j, AuthCodeInfoSend::Server& v) {
    if (j.contains("name") && !j["name"].is_null())
        j.at("name").get_to(v.name);
    if (j.contains("type") && !j["type"].is_null())
        j.at("type").get_to(v.type);
    if (j.contains("number") && !j["number"].is_null())
        j.at("number").get_to(v.number);
    if (j.contains("validDays") && !j["validDays"].is_null())
        j.at("validDays").get_to(v.validDays);
    if (j.contains("authorDays") && !j["authorDays"].is_null())
        j.at("authorDays").get_to(v.authorDays);
    if (j.contains("validDate") && !j["validDate"].is_null())
        j.at("validDate").get_to(v.validDate);
}

void to_json(nlohmann::json& j, const AuthCodeInfoSend::Server& v) {
    j["name"]       = v.name;
    j["type"]       = v.type;
    j["number"]     = v.number;
    j["validDays"]  = v.validDays;
    j["authorDays"] = v.authorDays;
    j["validDate"]  = v.validDate;
}

void from_json(const nlohmann::json& j, AuthCodeInfoSend::ResData& v) {
    if (j.contains("status") && !j["status"].is_null())
        j.at("status").get_to(v.status);
    if (j.contains("serverList") && !j["serverList"].is_null())
        j.at("serverList").get_to(v.serverList);
}

void to_json(nlohmann::json& j, const AuthCodeInfoSend::ResData& v) {
    j["status"]     = v.status;
    j["serverList"] = v.serverList;
}

void from_json(const nlohmann::json& j, LoginHttpResponse::ResData& v) {
    if (j.contains("token") && !j["token"].is_null())
        j.at("token").get_to(v.token);
    if (j.contains("deviceSn") && !j["deviceSn"].is_null())
        j.at("deviceSn").get_to(v.deviceSn);
}

void to_json(nlohmann::json& j, const LoginHttpResponse::ResData& v) {
    j["token"]    = v.token;
    j["deviceSn"] = v.deviceSn;
}

void from_json(const nlohmann::json& j, PasswordFile::UserPasswd& v) {
    if (j.contains("admin") && !j["admin"].is_null())
        j.at("admin").get_to(v.admin);
}

void to_json(nlohmann::json& j, const PasswordFile::UserPasswd& v) {
    j["admin"] = v.admin;
}

void to_json(nlohmann::json& j, const PasswordFile& v) {
    j["userPasswd"] = v.userPasswd;
}

void from_json(const nlohmann::json& j, PasswordFile& v) {
    if (j.contains("userPasswd") && !j["userPasswd"].is_null())
        j.at("userPasswd").get_to(v.userPasswd);
}

}  // namespace cosmo::service
