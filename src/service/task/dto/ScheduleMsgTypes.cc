// ScheduleMsgTypes — Schedule types — MsgScheduleTemplate.

#include "service/task/dto/ScheduleMsgTypes.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void to_json(nlohmann::json& j, const MsgScheduleConfig& v) {
    j["weekDay"] = v.weekDay;
    j["runTime"] = v.runTime;
}

void from_json(const nlohmann::json& j, MsgScheduleConfig& v) {
    j.at("weekDay").get_to(v.weekDay);
    j.at("runTime").get_to(v.runTime);
}

void to_json(nlohmann::json& j, const MsgScheduleTemplate& v) {
    j["scheduleName"]   = v.scheduleName;
    j["scheduleConfig"] = v.scheduleConfig;
    j["groupId"]        = v.groupId;
    j["scheduleId"]     = v.scheduleId;
    j["remark"]         = v.remark;
}

void from_json(const nlohmann::json& j, MsgScheduleTemplate& v) {
    j.at("scheduleName").get_to(v.scheduleName);
    j.at("scheduleConfig").get_to(v.scheduleConfig);
    JSON_OPT(j, v, groupId);
    JSON_OPT(j, v, scheduleId);
    JSON_OPT(j, v, remark);
}

}  // namespace cosmo
