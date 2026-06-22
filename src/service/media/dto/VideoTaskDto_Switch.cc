// VideoTaskDto_Switch — Video Task Dto_ Switch implementation.

#include <nlohmann/json.hpp>

#include "VideoTaskDto.h"
#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Switch and delete operations serialization (split from VideoTaskDto.cc)
namespace cosmo::VideoTask {

void to_json(nlohmann::json& j, const MsgSwitchTaskRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    to_json(j, static_cast<const MsgChannelTask&>(v));
    j["switch"] = v.enable;
}

void from_json(const nlohmann::json& j, MsgSwitchTaskRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    from_json(j, static_cast<MsgChannelTask&>(v));
    JSON_OPT_KEY(j, v, "switch", enable);
}

void to_json(nlohmann::json& j, const MsgTaskSwitch& v) {
    to_json(j, static_cast<const MsgChannelTask&>(v));
    j["switch"] = v.enable;
}

void from_json(const nlohmann::json& j, MsgTaskSwitch& v) {
    from_json(j, static_cast<MsgChannelTask&>(v));
    JSON_OPT_KEY(j, v, "switch", enable);
}

void to_json(nlohmann::json& j, const MsgBatchSwitchTaskRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["tasks"] = v.tasks;
}

void from_json(const nlohmann::json& j, MsgBatchSwitchTaskRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, tasks);
}

void to_json(nlohmann::json& j, const MsgBatchSwitchTaskSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgBatchSwitchTaskSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgQuerySwitchRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    to_json(j, static_cast<const MsgChannelTask&>(v));
}

void from_json(const nlohmann::json& j, MsgQuerySwitchRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    from_json(j, static_cast<MsgChannelTask&>(v));
}

void to_json(nlohmann::json& j, const MsgQuerySwitchSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQuerySwitchSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void from_json(const nlohmann::json& j, SwitchResData& v) {
    JSON_OPT(j, v, enable);
}

void to_json(nlohmann::json& j, const SwitchResData& v) {
    j["enable"] = v.enable;
}

void from_json(const nlohmann::json& j, MsgBatchSwitchTaskSend::ResData& v) {
    JSON_OPT(j, v, failedList);
}

void to_json(nlohmann::json& j, const MsgBatchSwitchTaskSend::ResData& v) {
    j["failedList"] = v.failedList;
}

void to_json(nlohmann::json& j, const MsgDeleteRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    to_json(j, static_cast<const MsgChannelTask&>(v));
}

void from_json(const nlohmann::json& j, MsgDeleteRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    from_json(j, static_cast<MsgChannelTask&>(v));
}

void to_json(nlohmann::json& j, const MsgBatchDeleteRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["tasks"] = v.tasks;
}

void from_json(const nlohmann::json& j, MsgBatchDeleteRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, tasks);
}

void to_json(nlohmann::json& j, const MsgBatchDeleteSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgBatchDeleteSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void from_json(const nlohmann::json& j, MsgBatchDeleteSend::ResData& v) {
    JSON_OPT(j, v, failedList);
}

void to_json(nlohmann::json& j, const MsgBatchDeleteSend::ResData& v) {
    j["failedList"] = v.failedList;
}

}  // namespace cosmo::VideoTask
