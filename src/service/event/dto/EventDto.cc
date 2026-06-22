// EventDto — Event DTO definitions (extracted from MessageEventHandler.h)

#include "EventDto.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo::Event {
void to_json(nlohmann::json& j, const MsgPageRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    to_json(j, static_cast<const MsgConditionEvent&>(v));
}

void from_json(const nlohmann::json& j, MsgPageRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    from_json(j, static_cast<MsgConditionEvent&>(v));
}

void to_json(nlohmann::json& j, const MsgExportAlarmRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    to_json(j, static_cast<const MsgConditionEvent&>(v));
}

void from_json(const nlohmann::json& j, MsgExportAlarmRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    from_json(j, static_cast<MsgConditionEvent&>(v));
}

void to_json(nlohmann::json& j, const MsgPageSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgPageSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgExportAlarmSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgExportAlarmSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgQueryPassengerFlowNumberRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["channelId"]     = v.channelId;
    j["algorithmCode"] = v.algorithmCode;
    j["type"]          = v.type;
    j["startTime"]     = v.startTime;
    j["endTime"]       = v.endTime;
}

void from_json(const nlohmann::json& j, MsgQueryPassengerFlowNumberRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, channelId);
    JSON_OPT(j, v, algorithmCode);
    JSON_OPT(j, v, type);
    JSON_OPT(j, v, startTime);
    JSON_OPT(j, v, endTime);
}

void to_json(nlohmann::json& j, const MsgQueryPassengerFlowNumberSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryPassengerFlowNumberSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void from_json(const nlohmann::json& j, MsgPageSend::ResData& v) {
    JSON_OPT(j, v, total);
    JSON_OPT(j, v, rows);
}

void to_json(nlohmann::json& j, const MsgPageSend::ResData& v) {
    j["total"] = v.total;
    j["rows"]  = v.rows;
}

void from_json(const nlohmann::json& j, MsgExportAlarmSend::ResData& v) {
    JSON_OPT(j, v, fileUrl);
}

void to_json(nlohmann::json& j, const MsgExportAlarmSend::ResData& v) {
    j["fileUrl"] = v.fileUrl;
}

void from_json(const nlohmann::json& j, MsgQueryPassengerFlowNumberSend::TimePoint& v) {
    JSON_OPT(j, v, timeString);
    JSON_OPT(j, v, enterNumber);
    JSON_OPT(j, v, leaveNumber);
}

void to_json(nlohmann::json& j, const MsgQueryPassengerFlowNumberSend::TimePoint& v) {
    j["timeString"]  = v.timeString;
    j["enterNumber"] = v.enterNumber;
    j["leaveNumber"] = v.leaveNumber;
}

void from_json(const nlohmann::json& j, MsgQueryPassengerFlowNumberSend::ResData& v) {
    JSON_OPT(j, v, totalCount);
    JSON_OPT(j, v, numberList);
}

void to_json(nlohmann::json& j, const MsgQueryPassengerFlowNumberSend::ResData& v) {
    j["totalCount"] = v.totalCount;
    j["numberList"] = v.numberList;
}

}  // namespace cosmo::Event
