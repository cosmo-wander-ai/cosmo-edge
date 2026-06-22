// ScheduleDto — Schedule DTO definitions (extracted from MessageScheduleHandler.h)

#include "service/task/dto/ScheduleDto.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo::Schedule {
void to_json(nlohmann::json& j, const MsgAddRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    to_json(j, static_cast<const MsgScheduleTemplate&>(v));
}

void from_json(const nlohmann::json& j, MsgAddRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    from_json(j, static_cast<MsgScheduleTemplate&>(v));
}

void to_json(nlohmann::json& j, const MsgAddSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgAddSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgUpdateRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    to_json(j, static_cast<const MsgScheduleTemplate&>(v));
}

void from_json(const nlohmann::json& j, MsgUpdateRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    from_json(j, static_cast<MsgScheduleTemplate&>(v));
}

void to_json(nlohmann::json& j, const MsgPageRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["groupId"]  = v.groupId;
    j["pageNum"]  = v.pageNum;
    j["pageSize"] = v.pageSize;
}

void from_json(const nlohmann::json& j, MsgPageRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, groupId);
    JSON_OPT(j, v, pageNum);
    JSON_OPT(j, v, pageSize);
}

void to_json(nlohmann::json& j, const MsgPageSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgPageSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgSelectScheduleInfoSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgSelectScheduleInfoSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgDeleteRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["scheduleId"] = v.scheduleId;
}

void from_json(const nlohmann::json& j, MsgDeleteRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("scheduleId").get_to(v.scheduleId);
}

void from_json(const nlohmann::json& j, MsgAddSend::ResData& v) {
    JSON_OPT(j, v, id);
}

void to_json(nlohmann::json& j, const MsgAddSend::ResData& v) {
    j["id"] = v.id;
}

void from_json(const nlohmann::json& j, MsgPageSend::ResData& v) {
    JSON_OPT(j, v, total);
    JSON_OPT(j, v, rows);
}

void to_json(nlohmann::json& j, const MsgPageSend::ResData& v) {
    j["total"] = v.total;
    j["rows"]  = v.rows;
}

void from_json(const nlohmann::json& j, MsgSelectScheduleInfoSend::ResData& v) {
    JSON_OPT(j, v, total);
    JSON_OPT(j, v, rows);
}

void to_json(nlohmann::json& j, const MsgSelectScheduleInfoSend::ResData& v) {
    j["total"] = v.total;
    j["rows"]  = v.rows;
}

}  // namespace cosmo::Schedule
