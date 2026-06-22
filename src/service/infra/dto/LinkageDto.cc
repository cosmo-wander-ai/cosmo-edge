// LinkageDto — Linkage DTO definitions

#include "LinkageDto.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

namespace cosmo {
void from_json(const nlohmann::json& j, LinkageStrategyOutputUnit& v) {
    JSON_OPT(j, v, id);
    JSON_OPT(j, v, name);
    JSON_OPT(j, v, workFlow);
    JSON_OPT(j, v, status);
}

void to_json(nlohmann::json& j, const LinkageStrategyOutputUnit& v) {
    j["id"]       = v.id;
    j["name"]     = v.name;
    j["workFlow"] = v.workFlow;
    j["status"]   = v.status;
}
}  // namespace cosmo

// Auto-generated JSON serialization
namespace cosmo::Linkage {
void to_json(nlohmann::json& j, const MsgStoragesSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgStoragesSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgAddRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["name"]     = v.name;
    j["workFlow"] = v.workFlow;
}

void from_json(const nlohmann::json& j, MsgAddRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, name);
    JSON_OPT(j, v, workFlow);
}

void to_json(nlohmann::json& j, const MsgAddSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgAddSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgDeleteRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["id"] = v.id;
}

void from_json(const nlohmann::json& j, MsgDeleteRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, id);
}

void to_json(nlohmann::json& j, const MsgUpdateRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["name"]     = v.name;
    j["id"]       = v.id;
    j["workFlow"] = v.workFlow;
}

void from_json(const nlohmann::json& j, MsgUpdateRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, name);
    JSON_OPT(j, v, id);
    JSON_OPT(j, v, workFlow);
}

void to_json(nlohmann::json& j, const MsgPageRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    to_json(j, static_cast<const MsgConditionPage&>(v));
    j["name"] = v.name;
}

void from_json(const nlohmann::json& j, MsgPageRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    from_json(j, static_cast<MsgConditionPage&>(v));
    JSON_OPT(j, v, name);
}

void to_json(nlohmann::json& j, const MsgPageSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgPageSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgSwitchRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["id"]     = v.id;
    j["switch"] = v.enable;
}

void from_json(const nlohmann::json& j, MsgSwitchRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, id);
    JSON_OPT_KEY(j, v, "switch", enable);
}

void from_json(const nlohmann::json& j, MsgStoragesSend::ResData& v) {
    JSON_OPT_KEY(j, v, "strages", storages);
}

void to_json(nlohmann::json& j, const MsgStoragesSend::ResData& v) {
    j["strages"] = v.storages;
}

void from_json(const nlohmann::json& j, MsgAddSend::ResData& v) {
    JSON_OPT(j, v, id);
}

void to_json(nlohmann::json& j, const MsgAddSend::ResData& v) {
    j["id"] = v.id;
}

void from_json(const nlohmann::json& j, MsgPageSend::ResData& v) {
    JSON_OPT(j, v, total);
    JSON_OPT(j, v, tasks);
}

void to_json(nlohmann::json& j, const MsgPageSend::ResData& v) {
    j["total"] = v.total;
    j["tasks"] = v.tasks;
}

}  // namespace cosmo::Linkage
