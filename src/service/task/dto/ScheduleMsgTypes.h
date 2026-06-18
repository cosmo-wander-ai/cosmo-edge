// Schedule types — MsgScheduleTemplate.
// Modular DTO header.

#pragma once

#include "util/dto/TaskAreaTypes.h"

namespace cosmo {

struct MsgScheduleConfig {
    util::RangeInt<0, 7> weekDay;
    std::vector<MsgRunTime> runTime;
};

void to_json(nlohmann::json& j, const MsgScheduleConfig& v);
void from_json(const nlohmann::json& j, MsgScheduleConfig& v);

struct MsgScheduleTemplate {
    bool is_default{false};  // Whether this is the default schedule
    std::string scheduleName;
    std::string scheduleId;
    std::string groupId;
    std::string remark;
    std::vector<MsgScheduleConfig> scheduleConfig;
};

void to_json(nlohmann::json& j, const MsgScheduleTemplate& v);
void from_json(const nlohmann::json& j, MsgScheduleTemplate& v);

}  // namespace cosmo
