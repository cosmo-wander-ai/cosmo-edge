// AuthDto — Auth DTO definitions (extracted from MessageAuthHandler.h)

#include "AuthDto.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo::Auth {
void to_json(nlohmann::json& j, const MsgDoLoginRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["account"] = v.account;
    j["pwd"]     = v.pwd;
}

void from_json(const nlohmann::json& j, MsgDoLoginRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, account);
    JSON_OPT(j, v, pwd);
}

void to_json(nlohmann::json& j, const MsgDoLoginSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgDoLoginSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgModifyPasswordRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["mtk"]       = v.mtk;
    j["passwdOld"] = v.passwdOld;
    j["passwdNew"] = v.passwdNew;
}

void from_json(const nlohmann::json& j, MsgModifyPasswordRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, mtk);
    JSON_OPT(j, v, passwdOld);
    JSON_OPT(j, v, passwdNew);
}

void from_json(const nlohmann::json& j, MsgDoLoginSend::ResData& v) {
    JSON_OPT(j, v, accountName);
    JSON_OPT(j, v, mtk);
}

void to_json(nlohmann::json& j, const MsgDoLoginSend::ResData& v) {
    j["accountName"] = v.accountName;
    j["mtk"]         = v.mtk;
}

}  // namespace cosmo::Auth
