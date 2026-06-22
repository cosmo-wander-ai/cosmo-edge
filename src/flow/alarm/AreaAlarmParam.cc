// AreaAlarmParam.cc — Parameter parsing / modification / assignment.
// Implementation partition of AreaAlarm (declared in flow/alarm/AreaAlarm.h).

#include <functional>

#include "flow/alarm/AreaAlarm.h"
#include "flow/alarm/AreaAlarmInternalTypes.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/SafeParse.h"

static constexpr const char* kTag = "AreaAlarm ";

namespace cosmo {

void AreaAlarm::SetMultTargetAreaLimitTargetCount(const MsgDynamicKeyValue& param, const std::string& label) {
    auto value = util::ParseInt(param.value);
    if (!ValidateLimitTargetCount(value)) {
        LOG_WARN(
            "ModifyParam "
            "[{}] {}/{} param.key {} value {} is Illegal",
            task_id, action_info_.actionName, action_info_.flowActionId, param.key, param.value);
        return;
    }

    auto it =
        std::find_if(params_.mult_target_limit_param.begin(), params_.mult_target_limit_param.end(),
                     [&](const MultTargetAreaLimitParam& labelParam) { return labelParam.label == label; });
    if (it != params_.mult_target_limit_param.end()) {
        it->area_limit_target_count = value;
        LOG_INFO(
            "ModifyParam "
            "[{}] {}/{} Set {} To {}. srcValue Is {}",
            task_id, action_info_.actionName, action_info_.flowActionId, param.keys[1], value, param.value);
        return;
    }
    MultTargetAreaLimitParam paramUnit;
    paramUnit.label                   = label;
    paramUnit.area_limit_target_count = value;
    LOG_INFO(
        "ModifyParam "
        "[{}] {}/{} Set {} To {}. srcValue Is {}",
        task_id, action_info_.actionName, action_info_.flowActionId, param.keys[1], value, param.value);
    params_.mult_target_limit_param.push_back(paramUnit);
    return;
}

void AreaAlarm::SetMultTargetAreaLimitTargetType(const MsgDynamicKeyValue& param, const std::string& label) {
    auto value = util::ParseInt(param.value);
    if (!ValidateCompareType(value)) {
        LOG_WARN(
            "ModifyParam "
            "[{}] {}/{} param.key {} value {} is Illegal",
            task_id, action_info_.actionName, action_info_.flowActionId, param.key, param.value);
        return;
    }

    auto it =
        std::find_if(params_.mult_target_limit_param.begin(), params_.mult_target_limit_param.end(),
                     [&](const MultTargetAreaLimitParam& labelParam) { return labelParam.label == label; });
    if (it != params_.mult_target_limit_param.end()) {
        it->area_limit_target_type = static_cast<AlgCompareType>(value);
        LOG_INFO(
            "ModifyParam "
            "[{}] {}/{} Set {} To {}. srcValue Is {}",
            task_id, action_info_.actionName, action_info_.flowActionId, param.keys[1], value, param.value);
        return;
    }
    MultTargetAreaLimitParam paramUnit;
    paramUnit.label                  = label;
    paramUnit.area_limit_target_type = static_cast<AlgCompareType>(value);
    LOG_INFO(
        "ModifyParam "
        "[{}] {}/{} Set {} To {}. srcValue Is {}",
        task_id, action_info_.actionName, action_info_.flowActionId, param.keys[1], value, param.value);
    params_.mult_target_limit_param.push_back(paramUnit);
}

/*
param.areaLimitTargetCount
param.areaLimitTargetType
param.areaDuration
param.lines
param.breakLineType
param.targetCalcType
*/

// Parameter handler: returns true when validation passes and assignment is done
using ParamHandler = std::function<bool(const std::string& value, BAAreaAlarmParam& p)>;

// Single-parameter handler registry — replaces the if-else chain for keys.size()==2 in AnalysisKey
static const std::vector<std::pair<std::vector<std::string_view>, ParamHandler>> kParamHandlers = {
    // areaLimitTargetCount
    {{key::area::LIMIT_TARGET_COUNT},
     [](const std::string& v, BAAreaAlarmParam& p) -> bool {
         auto val = util::ParseInt(v);
         if (!ValidateLimitTargetCount(val))
             return false;
         p.area_limit_target_count = val;
         return true;
     }},

    // areaLimitTargetType
    {{key::area::LIMIT_TARGET_TYPE},
     [](const std::string& v, BAAreaAlarmParam& p) -> bool {
         auto val = util::ParseInt(v);
         if (!ValidateCompareType(val))
             return false;
         p.area_limit_target_type = static_cast<AlgCompareType>(val);
         return true;
     }},

    // areaDuration / areaLimitDuration / areaCalcDuration
    {{key::area::DURATION, key::area::LIMIT_DURATION, key::area::CALC_DURATION},
     [](const std::string& v, BAAreaAlarmParam& p) -> bool {
         auto val = util::ParseInt(v);
         if (!ValidateDuration(val))
             return false;
         p.area_duration_src = val;
         p.area_duration     = p.area_duration_src * p.area_duration_time_type;
         return true;
     }},

    // areaDurationTimeType / areaLimitDurationTimeType / areaCalcDurationTimeType
    {{key::area::DURATION_TIME_TYPE, key::area::LIMIT_DURATION_TIME_TYPE, key::area::CALC_DURATION_TIME_TYPE},
     [](const std::string& v, BAAreaAlarmParam& p) -> bool {
         auto val = util::ParseInt<long long>(v);
         if (val < 1)
             return false;
         p.area_duration_time_type = val;
         p.area_duration           = p.area_duration_src * p.area_duration_time_type;
         return true;
     }},

    // lines (placeholder)
    {{key::LINES}, [](const std::string& /*v*/, BAAreaAlarmParam& /*p*/) -> bool { return true; }},

    // breakLineType
    {{key::BREAK_LINE_TYPE},
     [](const std::string& v, BAAreaAlarmParam& p) -> bool {
         auto val = util::ParseInt(v);
         if (!ValidateBreakLineType(val))
             return false;
         p.break_line_type = static_cast<BreakLineType>(val);
         return true;
     }},

    // trippingWireType
    {{key::TRIPPING_WIRE_TYPE},
     [](const std::string& v, BAAreaAlarmParam& p) -> bool {
         auto val = util::ParseInt(v);
         if (val <= 0)
             return false;
         p.tripping_wire_type = val;
         return true;
     }},

    // targetCalcType
    {{key::TARGET_CALC_TYPE},
     [](const std::string& v, BAAreaAlarmParam& p) -> bool {
         auto val = util::ParseInt(v);
         if (!ValidateTargetCalcType(val))
             return false;
         p.target_calc_type = static_cast<TargetCalcType>(val);
         return true;
     }},

    // retroDirect
    {{key::RETRO_DIRECT},
     [](const std::string& v, BAAreaAlarmParam& p) -> bool {
         auto val = util::ParseInt(v);
         if (!ValidateRetroDirect(val))
             return false;
         p.retro_direct = static_cast<RetroDirect>(val);
         return true;
     }},

    // retroDistance
    {{key::RETRO_DISTANCE},
     [](const std::string& v, BAAreaAlarmParam& p) -> bool {
         auto val = util::ParseFloat(v);
         if ((val > 1.0f) || (val < 0.0f))
             return false;
         p.retro_distance = val;
         return true;
     }},

    // areaSensitivity
    {{key::area::SENSITIVITY},
     [](const std::string& v, BAAreaAlarmParam& p) -> bool {
         auto val = util::ParseInt(v);
         if ((val > 10) || (val < 1))
             return false;
         p.area_sensitivity       = val;
         p.area_sensitivity_ratio = (11.f - val) / 10.f;
         return true;
     }},
};

bool AreaAlarm::AnalysisKey(const MsgDynamicKeyValue& param, BAAreaAlarmParam& localParamEl) {
    if (param.keys.empty()) {
        LOG_WARN(
            "ModifyParam "
            "[{}] {}/{} param.keys is Empty",
            task_id, action_info_.actionName, action_info_.flowActionId);
        return false;
    }
    if (!((param.keys.size() == 2) || (param.keys.size() == 3))) {
        LOG_DEBUG(
            "ModifyParam "
            "[{}] {}/{} Set {} Failed. key size:{}",
            task_id, action_info_.actionName, action_info_.flowActionId, param.key, param.keys.size());
        return false;
    }
    if (key::PARAM != param.keys[0]) {
        LOG_DEBUG(
            "ModifyParam "
            "[{}] {}/{} param.keys[0] is Not {}",
            task_id, action_info_.actionName, action_info_.flowActionId, key::PARAM);
        return false;
    }

    // --- keys.size() == 2: table-driven lookup ---
    if (param.keys.size() == 2) {
        for (const auto& [keys, handler] : kParamHandlers) {
            for (const auto& key : keys) {
                if (param.keys[1] == key) {
                    if (!handler(param.value, localParamEl)) {
                        LOG_WARN(
                            "ModifyParam "
                            "[{}] {}/{} param.key {} value {} is Illegal",
                            task_id, action_info_.actionName, action_info_.flowActionId, param.key,
                            param.value);
                        return false;
                    }
                    LOG_INFO(
                        "ModifyParam "
                        "[{}] {}/{} Set {} To {}",
                        task_id, action_info_.actionName, action_info_.flowActionId, param.keys[1],
                        param.value);
                    return true;
                }
            }
        }
        return false;
    }

    // --- keys.size() == 3: multi-target parameters ---
    if (param.keys[1] == key::area::LIMIT_TARGET_COUNT) {
        SetMultTargetAreaLimitTargetCount(param, param.keys[2]);
        return true;
    }
    if (param.keys[1] == key::area::LIMIT_TARGET_TYPE) {
        SetMultTargetAreaLimitTargetType(param, param.keys[2]);
        return true;
    }
    return false;
}

// Modify parameters — incremental update on existing params
bool AreaAlarm::ModifyParam(const std::string& /*channelId*/, const std::string& /*taskId*/,
                            std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    for (auto& param : params) {
        AnalysisKey(param, params_);
    }

    LOG_INFO(
        "ModifyParam "
        "[{}] {}/{} Set areaLimitTargetCount:{} areaLimitTargetType:{} areaDurationSrc:{} areaDuration:{} "
        "TimeType:{} breakLineType:{} targetCalcType:{} retroDirect:{} retroDistance:{} ",
        task_id, action_info_.actionName, action_info_.flowActionId, params_.area_limit_target_count,
        params_.area_limit_target_type, params_.area_duration_src, params_.area_duration,
        params_.area_duration_time_type, params_.break_line_type, params_.target_calc_type,
        params_.retro_direct, params_.retro_distance);

    return false;
}

// Set parameters — clear previous params and apply full replacement
bool AreaAlarm::SetParam(const std::string& /*channelId*/, const std::string& /*taskId*/,
                         std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    // Clear existing params first
    params_ = {};
    for (auto& param : params) {
        AnalysisKey(param, params_);
    }
    LOG_INFO(
        "ModifyParam "
        "[{}] {}/{} Set areaLimitTargetCount:{} areaLimitTargetType:{} areaDurationSrc:{} areaDuration:{} "
        "TimeType:{} breakLineType:{} targetCalcType:{} retroDirect:{} retroDistance:{} ",
        task_id, action_info_.actionName, action_info_.flowActionId, params_.area_limit_target_count,
        params_.area_limit_target_type, params_.area_duration_src, params_.area_duration,
        params_.area_duration_time_type, params_.break_line_type, params_.target_calc_type,
        params_.retro_direct, params_.retro_distance);
    return false;
}

// Set areas — clear previous areas and apply full replacement
bool AreaAlarm::SetArea(const std::string& /*channelId*/, const std::string& taskId,
                        std::vector<MsgTaskArea>& areas, std::vector<MsgTaskArea>& shieldedAreas) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    has_area_                = !areas.empty();
    task_area_.taskId        = taskId;
    task_area_.areas         = areas;
    task_area_.shieldedAreas = shieldedAreas;
    // Synchronize with area_target_status_map_
    // 1. Remove areas in area_target_status_map_ that no longer exist in task_area_
    for (auto it = area_target_status_map_.begin(); it != area_target_status_map_.end();) {
        auto iterFind =
            std::find_if(task_area_.areas.begin(), task_area_.areas.end(),
                         [&, it](const MsgTaskArea& localArea) { return localArea.areaId == it->first; });
        // Not found in task_area_, meaning it is an extra area in area_target_status_map_
        if (iterFind == task_area_.areas.end()) {
            it = area_target_status_map_.erase(it);
        } else {
            ++it;
        }
    }

    // 2. Add areas from task_area_
    for (auto& area : task_area_.areas) {
        if ((MsgInputAreaType::Main == params_.input_area_type) ||
            (MsgInputAreaType::All == params_.input_area_type)) {
            area_target_status_map_[area.areaId].area_id   = area.areaId;
            area_target_status_map_[area.areaId].area_name = area.name;
        }

        if ((MsgInputAreaType::Asso == params_.input_area_type) ||
            (MsgInputAreaType::All == params_.input_area_type)) {
            for (auto& assoArea : area.associatedAreas) {
                area_target_status_map_[assoArea.areaId].area_id   = assoArea.areaId;
                area_target_status_map_[assoArea.areaId].area_name = assoArea.name;
            }
        }

        pass_flow_areas_map_[area.areaId].area_id   = area.areaId;
        pass_flow_areas_map_[area.areaId].area_name = area.name;
        if (area.associatedAreas.empty()) {
            pass_flow_areas_map_[area.areaId].associated_area = key::DEFAULT_AREA;
        } else {
            pass_flow_areas_map_[area.areaId].associated_area = area.associatedAreas[0].areaId;
        }
    }

    return true;
}

}  // namespace cosmo
