// SystemDebugDto — Thread debug info

#include "SystemDebugDto.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo::System {
void to_json(nlohmann::json& j, const MsgModifyDebugModeRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["debugModeOpen"] = v.debugModeOpen;
}

void from_json(const nlohmann::json& j, MsgModifyDebugModeRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, debugModeOpen);
}

void to_json(nlohmann::json& j, const MsgQueryDebugModeSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryDebugModeSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgModifyShiledActionsRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["shiledActions"] = v.shiledActions;
}

void from_json(const nlohmann::json& j, MsgModifyShiledActionsRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, shiledActions);
}

void to_json(nlohmann::json& j, const MsgQueryShiledActionsSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryShiledActionsSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgDictRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["keys"] = v.keys;
}

void from_json(const nlohmann::json& j, MsgDictRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, keys);
}

void to_json(nlohmann::json& j, const MsgDictSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgDictSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void from_json(const nlohmann::json& j, MsgQueryDebugModeSend::ResData& v) {
    JSON_OPT(j, v, debugModeOpen);
}

void to_json(nlohmann::json& j, const MsgQueryDebugModeSend::ResData& v) {
    j["debugModeOpen"] = v.debugModeOpen;
}

void from_json(const nlohmann::json& j, MsgQueryShiledActionsSend::ResData& v) {
    JSON_OPT(j, v, shiledActions);
}

void to_json(nlohmann::json& j, const MsgQueryShiledActionsSend::ResData& v) {
    j["shiledActions"] = v.shiledActions;
}

void from_json(const nlohmann::json& j, MsgDictUnit& v) {
    JSON_OPT(j, v, key);
    JSON_OPT(j, v, infos);
}

void to_json(nlohmann::json& j, const MsgDictUnit& v) {
    j["key"]   = v.key;
    j["infos"] = v.infos;
}

void from_json(const nlohmann::json& j, MsgDictSend::ResData& v) {
    JSON_OPT(j, v, infos);
}

void to_json(nlohmann::json& j, const MsgDictSend::ResData& v) {
    j["infos"] = v.infos;
}

}  // namespace cosmo::System
