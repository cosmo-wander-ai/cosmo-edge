// ServerMsgTypes — Create task response

#include "ServerMsgTypes.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void to_json(nlohmann::json& j, const MsgInterfaceTestRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["test"] = v.test;
}

void from_json(const nlohmann::json& j, MsgInterfaceTestRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("test").get_to(v.test);
}

void to_json(nlohmann::json& j, const MsgTaskCancleRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["taskId"]  = v.taskId;
    j["mvDebug"] = v.mvDebug;
}

void from_json(const nlohmann::json& j, MsgTaskCancleRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("taskId").get_to(v.taskId);  // mandatory
    JSON_OPT(j, v, mvDebug);
}

void to_json(nlohmann::json& j, const MsgPTaskCancleRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["algorithmCode"] = v.algorithmCode;
    j["mvDebug"]       = v.mvDebug;
    j["taskId"]        = v.taskId;
}

void from_json(const nlohmann::json& j, MsgPTaskCancleRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("algorithmCode").get_to(v.algorithmCode);  // mandatory
    JSON_OPT(j, v, mvDebug);
    JSON_OPT(j, v, taskId);
}

void to_json(nlohmann::json& j, const MsgOperateNodeRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["devId"]       = v.devId;
    j["operateType"] = v.operateType;
}

void from_json(const nlohmann::json& j, MsgOperateNodeRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, devId);
    JSON_OPT(j, v, operateType);
}

void to_json(nlohmann::json& j, const MsgInfoRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["devId"] = v.devId;
}

void from_json(const nlohmann::json& j, MsgInfoRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("devId").get_to(v.devId);
}

void to_json(nlohmann::json& j, const MsgInfoSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    to_json(j, static_cast<const CMsgHeartBeatReq&>(v));
}

void from_json(const nlohmann::json& j, MsgInfoSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    from_json(j, static_cast<CMsgHeartBeatReq&>(v));
}

void to_json(nlohmann::json& j, const MsgGraphicsMemoryRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["test"] = v.test;
}

void from_json(const nlohmann::json& j, MsgGraphicsMemoryRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("test").get_to(v.test);
}

void to_json(nlohmann::json& j, const MsgGraphicsMemorySend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["debugMessage"] = v.debugMessage;
}

void from_json(const nlohmann::json& j, MsgGraphicsMemorySend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    j.at("debugMessage").get_to(v.debugMessage);
}

void to_json(nlohmann::json& j, const MsgViewRoutesRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["viewCounts"] = v.viewCounts;
}

void from_json(const nlohmann::json& j, MsgViewRoutesRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("viewCounts").get_to(v.viewCounts);
}

}  // namespace cosmo
