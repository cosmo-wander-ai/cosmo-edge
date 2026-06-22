// ClientMsgTask — Client-side task query message types.

#include "ClientMsgTask.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void to_json(nlohmann::json& j, const CMsgQueryTaskListReq& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["devId"] = v.devId;
}

void from_json(const nlohmann::json& j, CMsgQueryTaskListReq& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, devId);
}

void to_json(nlohmann::json& j, const CMsgQueryTaskListRsp& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, CMsgQueryTaskListRsp& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const CMsgOnCompleteReq& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["taskId"] = v.taskId;
}

void from_json(const nlohmann::json& j, CMsgOnCompleteReq& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, taskId);
}

void from_json(const nlohmann::json& j, CMsgQueryTaskListRsp::ResData& v) {
    JSON_OPT(j, v, task);
}

void to_json(nlohmann::json& j, const CMsgQueryTaskListRsp::ResData& v) {
    j["task"] = v.task;
}

}  // namespace cosmo
