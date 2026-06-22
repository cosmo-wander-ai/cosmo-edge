// SystemConfigDto — System config data types — extracted from util/config/CfgAlarmParam.h,

#include "SystemConfigDto.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void from_json(const nlohmann::json& j, CfgAlarmParamOverviewInfo& v) {
    JSON_OPT(j, v, picQuality);
    JSON_OPT(j, v, alarmTypeOverview);
    JSON_OPT(j, v, areaOverview);
    JSON_OPT(j, v, targetBoxOverview);
    JSON_OPT(j, v, targetSizeOverview);
}

void to_json(nlohmann::json& j, const CfgAlarmParamOverviewInfo& v) {
    j["picQuality"]         = v.picQuality;
    j["alarmTypeOverview"]  = v.alarmTypeOverview;
    j["areaOverview"]       = v.areaOverview;
    j["targetBoxOverview"]  = v.targetBoxOverview;
    j["targetSizeOverview"] = v.targetSizeOverview;
}

void from_json(const nlohmann::json& j, CfgAlarmParamVideoRecordInfo& v) {
    JSON_OPT(j, v, bopen);
    JSON_OPT(j, v, preDuration);
    JSON_OPT(j, v, aftreDuration);
}

void to_json(nlohmann::json& j, const CfgAlarmParamVideoRecordInfo& v) {
    j["bopen"]         = v.bopen;
    j["preDuration"]   = v.preDuration;
    j["aftreDuration"] = v.aftreDuration;
}

void from_json(const nlohmann::json& j, CfgRebootParamInfo& v) {
    JSON_OPT(j, v, isTimingRestart);
    JSON_OPT(j, v, weekDay);
    JSON_OPT(j, v, restartTimeSec);
}

void to_json(nlohmann::json& j, const CfgRebootParamInfo& v) {
    j["isTimingRestart"] = v.isTimingRestart;
    j["weekDay"]         = v.weekDay;
    j["restartTimeSec"]  = v.restartTimeSec;
}

}  // namespace cosmo
