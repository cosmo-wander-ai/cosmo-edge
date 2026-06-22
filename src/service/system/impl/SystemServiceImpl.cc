// SystemServiceImpl — System Service Impl implementation.

#include "service/system/impl/SystemServiceImpl.h"

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <nlohmann/json.hpp>
#include <shared_mutex>

#include "service/detail/ServiceRegistry.h"
#include "service/event/IAlarmPushService.h"
#include "service/network/INetworkService.h"
#include "service/system/ISystemOperationService.h"
#include "service/system/dto/SystemConfigTypes.h"
#include "service/system/dto/SystemDebugDto.h"
#include "service/system/dto/SystemDeviceDto.h"
#include "service/system/dto/SystemMaintainDto.h"
#include "service/system/dto/SystemNetworkDto.h"
#include "service/system/dto/SystemParamDto.h"
#include "service/system/dto/SystemTimeDto.h"
#include "util/ErrorCode.h"
#include "util/FileUtil.h"
#include "util/JsonStructUtil.h"
#include "util/LimitedTypeJson.h"
#include "util/Log.h"
#include "util/PathUtil.h"

namespace cosmo::service {

// Serialization envelope for alarmParam.json (migrated from CfgAlarmParam)
struct CfgAlarmParamInfo {
    cosmo::CfgAlarmParamOverviewInfo overviewInfo;
    cosmo::CfgAlarmParamVideoRecordInfo videoRecordInfo;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(CfgAlarmParamInfo, overviewInfo, videoRecordInfo)
};

// Internal state migrated from CfgSystemParam singleton
struct SystemServiceImpl::SysConfigState {
    std::shared_mutex mtx;
    std::string conf_file_name{"devSystemParam.json"};
    cosmo::CfgSystemParamInfo config;
    cosmo::CfgSystemTmpParam tmp;

    SysConfigState() {
        auto cfg_path = (std::filesystem::path(cosmo::path::GetCfgPath()) / conf_file_name).string();
        static_cast<void>(cosmo::util::LoadStructFromJsonFile(cfg_path, config));
        LOG_INFO("{}", "SysConfigState: system config loaded");
    }

    void SaveCfg() {
        auto path = (std::filesystem::path(cosmo::path::GetCfgPath()) / conf_file_name).string();
        static_cast<void>(cosmo::util::SaveStructToJsonFile(path, config));
    }
};

SystemServiceImpl::SystemServiceImpl() : sys_config_state_(std::make_unique<SysConfigState>()) {
    CfgAlarmParamInfo cfg;
    auto alarm_cfg_path = (std::filesystem::path(cosmo::path::GetCfgPath()) / alarm_cfg_file_).string();
    static_cast<void>(cosmo::util::LoadStructFromJsonFile(alarm_cfg_path, cfg));
    overview_config_     = cfg.overviewInfo;
    video_record_config_ = cfg.videoRecordInfo;
    LOG_INFO("{}", "SystemServiceImpl: alarm config loaded");

    auto reboot_cfg_path = (std::filesystem::path(cosmo::path::GetCfgPath()) / reboot_cfg_file_).string();
    static_cast<void>(cosmo::util::LoadStructFromJsonFile(reboot_cfg_path, reboot_config_));
    LOG_INFO("{}", "SystemServiceImpl: reboot config loaded");
}

SystemServiceImpl::~SystemServiceImpl() = default;

void SystemServiceImpl::SaveAlarmCfg() {
    CfgAlarmParamInfo cfg;
    cfg.overviewInfo    = overview_config_;
    cfg.videoRecordInfo = video_record_config_;

    auto path = (std::filesystem::path(cosmo::path::GetCfgPath()) / alarm_cfg_file_).string();
    static_cast<void>(cosmo::util::SaveStructToJsonFile(path, cfg));
}

void SystemServiceImpl::SaveRebootCfg() {
    auto path = (std::filesystem::path(cosmo::path::GetCfgPath()) / reboot_cfg_file_).string();
    static_cast<void>(cosmo::util::SaveStructToJsonFile(path, reboot_config_));
}

cosmo::CfgAlarmParamOverviewInfo SystemServiceImpl::GetPictureQuality() {
    std::shared_lock<std::shared_mutex> lock(alarm_mtx_);
    return overview_config_;
}

cosmo::util::ErrorEnum SystemServiceImpl::SetPictureQuality(cosmo::CfgAlarmParamOverviewInfo info) {
    if ((info.picQuality <= 0) || (info.picQuality > 100)) {
        return cosmo::util::ErrorEnum::ParameterException;
    }
    std::lock_guard<std::shared_mutex> lock(alarm_mtx_);
    if ((info.picQuality != overview_config_.picQuality) ||
        (info.alarmTypeOverview != overview_config_.alarmTypeOverview) ||
        (info.areaOverview != overview_config_.areaOverview) ||
        (info.targetBoxOverview != overview_config_.targetBoxOverview) ||
        (info.targetSizeOverview != overview_config_.targetSizeOverview)) {
        overview_config_ = info;
        SaveAlarmCfg();
    }
    return cosmo::util::ErrorEnum::Success;
}

cosmo::util::ErrorEnum SystemServiceImpl::ResetPictureQuality() {
    cosmo::CfgAlarmParamOverviewInfo info;
    std::lock_guard<std::shared_mutex> lock(alarm_mtx_);
    overview_config_ = info;
    SaveAlarmCfg();
    return cosmo::util::ErrorEnum::Success;
}

cosmo::CfgAlarmParamVideoRecordInfo SystemServiceImpl::GetAlarmVideoDuration() {
    std::shared_lock<std::shared_mutex> lock(alarm_mtx_);
    return video_record_config_;
}

cosmo::util::ErrorEnum SystemServiceImpl::SetAlarmVideoDuration(cosmo::CfgAlarmParamVideoRecordInfo info) {
    if ((info.preDuration <= 0) || (info.preDuration > 100)) {
        return cosmo::util::ErrorEnum::ParameterException;
    }
    if ((info.aftreDuration <= 0) || (info.aftreDuration > 100)) {
        return cosmo::util::ErrorEnum::ParameterException;
    }
    std::lock_guard<std::shared_mutex> lock(alarm_mtx_);
    if ((info.bopen != video_record_config_.bopen) ||
        (info.preDuration != video_record_config_.preDuration) ||
        (info.aftreDuration != video_record_config_.aftreDuration)) {
        video_record_config_ = info;
        SaveAlarmCfg();
    }
    return cosmo::util::ErrorEnum::Success;
}

cosmo::util::ErrorEnum SystemServiceImpl::ResetAlarmVideoDuration() {
    cosmo::CfgAlarmParamVideoRecordInfo info;
    std::lock_guard<std::shared_mutex> lock(alarm_mtx_);
    video_record_config_ = info;
    SaveAlarmCfg();
    return cosmo::util::ErrorEnum::Success;
}

cosmo::CfgRebootParamInfo SystemServiceImpl::GetRebootParam() {
    std::shared_lock<std::shared_mutex> lock(reboot_mtx_);
    return reboot_config_;
}

cosmo::util::ErrorEnum SystemServiceImpl::SetRebootParam(cosmo::CfgRebootParamInfo info) {
    if ((info.weekDay < 0) || (info.weekDay > 7)) {
        return cosmo::util::ErrorEnum::ParameterException;
    }
    std::lock_guard<std::shared_mutex> lock(reboot_mtx_);
    if ((info.isTimingRestart != reboot_config_.isTimingRestart) ||
        (info.weekDay != reboot_config_.weekDay) || (info.restartTimeSec != reboot_config_.restartTimeSec)) {
        reboot_config_ = info;
        SaveRebootCfg();
    }
    return cosmo::util::ErrorEnum::Success;
}

cosmo::util::ErrorEnum SystemServiceImpl::ResetRebootParam() {
    cosmo::CfgRebootParamInfo info;
    std::lock_guard<std::shared_mutex> lock(reboot_mtx_);
    reboot_config_ = info;
    SaveRebootCfg();
    return cosmo::util::ErrorEnum::Success;
}

SystemLogoInfo SystemServiceImpl::GetSystemLogo() {
    SystemLogoInfo info;
    std::shared_lock<std::shared_mutex> lock(sys_config_state_->mtx);
    if (sys_config_state_->config.logoInfo.bHaveSetLogo) {
        info.systemName = sys_config_state_->config.logoInfo.systemName;
        info.logoUrl    = (std::filesystem::path(cosmo::path::GetLogoWebPath()) /
                        sys_config_state_->config.logoInfo.logoFileName)
                           .string();
    }
    info.bigScreenName = sys_config_state_->config.logoInfo.bigScreenName;
    return info;
}

cosmo::util::ErrorEnum SystemServiceImpl::SetSystemLogo(const std::string& systemName,
                                                        const std::string& logoFileType,
                                                        const std::vector<uint8_t>& logo_img,
                                                        const std::string& bigScreenName) {
    std::string logo_name = "logo" + logoFileType;
    std::string logo_path;
    {
        std::lock_guard<std::shared_mutex> lock(sys_config_state_->mtx);
        sys_config_state_->config.logoInfo.systemName   = systemName;
        sys_config_state_->config.logoInfo.logoFileName = logo_name;
        sys_config_state_->config.logoInfo.bHaveSetLogo = true;
        if (!bigScreenName.empty()) {
            sys_config_state_->config.logoInfo.bigScreenName = bigScreenName;
        }
        sys_config_state_->SaveCfg();
        logo_path = (std::filesystem::path(cosmo::path::GetLogoPath()) / logo_name).string();
    }
    cosmo::util::WriteFile(logo_path, reinterpret_cast<const std::uint8_t*>(logo_img.data()),
                           static_cast<int>(logo_img.size()));
    return {};
}

bool SystemServiceImpl::GetDebugMode() {
    std::shared_lock<std::shared_mutex> lock(sys_config_state_->mtx);
    return sys_config_state_->tmp.debugInfo.bDebugOpen;
}

void SystemServiceImpl::SetDebugMode(bool enable) {
    std::lock_guard<std::shared_mutex> lock(sys_config_state_->mtx);
    sys_config_state_->tmp.debugInfo.bDebugOpen = enable;
}

std::vector<std::string> SystemServiceImpl::GetShieldedActions() {
    std::shared_lock<std::shared_mutex> lock(sys_config_state_->mtx);
    return sys_config_state_->tmp.debugInfo.shieldActions;
}

void SystemServiceImpl::SetShieldedActions(const std::vector<std::string>& actions) {
    std::lock_guard<std::shared_mutex> lock(sys_config_state_->mtx);
    sys_config_state_->tmp.debugInfo.shieldActions = actions;
}

void SystemServiceImpl::GetPopUpParam(int& pop_up_switch, int& audio_play, int& pop_up_duration) {
    std::shared_lock<std::shared_mutex> lock(sys_config_state_->mtx);
    pop_up_switch   = sys_config_state_->config.popUpInfo.popUpSwitch;
    audio_play      = sys_config_state_->config.popUpInfo.audioPlay;
    pop_up_duration = sys_config_state_->config.popUpInfo.popUpDuration;
}

void SystemServiceImpl::SetPopUpParam(int pop_up_switch, int audio_play, int pop_up_duration) {
    std::lock_guard<std::shared_mutex> lock(sys_config_state_->mtx);
    if ((pop_up_switch != sys_config_state_->config.popUpInfo.popUpSwitch) ||
        (pop_up_duration != sys_config_state_->config.popUpInfo.popUpDuration) ||
        (audio_play != sys_config_state_->config.popUpInfo.audioPlay)) {
        sys_config_state_->config.popUpInfo.popUpSwitch   = pop_up_switch;
        sys_config_state_->config.popUpInfo.popUpDuration = pop_up_duration;
        sys_config_state_->config.popUpInfo.audioPlay     = audio_play;
        sys_config_state_->SaveCfg();
    }
}

HttpPushParam SystemServiceImpl::GetHttpInterfaceParam() {
    auto& svc = service::ServiceRegistry::Instance().Get<service::IAlarmPushService>();
    return {svc.IsEnabled(), svc.GetUrl()};
}

cosmo::util::ErrorEnum SystemServiceImpl::SetHttpInterfaceParam(const HttpPushParam& param) {
    return service::ServiceRegistry::Instance().Get<service::IAlarmPushService>().SetPush(param.enable,
                                                                                          param.url);
}

MqttParam SystemServiceImpl::GetMqttParam() {
    MqttParam result;
    {
        std::shared_lock<std::shared_mutex> lock(sys_config_state_->mtx);
        auto& info      = sys_config_state_->config.interfaceModeInfo.standAloneParam;
        result.enable   = info.mqttParam.bEnable;
        result.url      = info.mqttParam.ip;
        result.port     = info.mqttParam.port;
        result.authMode = info.mqttParam.authMode;
        result.clientId = info.mqttParam.clientId;
        result.userName = info.mqttParam.userName;
        result.passwd   = info.mqttParam.passwd;
    }
    result.status = service::ServiceRegistry::Instance().Get<service::INetworkService>().IsMqttRegistered();
    return result;
}

cosmo::util::ErrorEnum SystemServiceImpl::SetMqttParam(const MqttParam& param) {
    bool changed = false;
    {
        std::lock_guard<std::shared_mutex> lock(sys_config_state_->mtx);
        auto& mqtt = sys_config_state_->config.interfaceModeInfo.standAloneParam.mqttParam;
        if ((param.enable != mqtt.bEnable) || (param.url != mqtt.ip) || (param.port != mqtt.port) ||
            (param.authMode != mqtt.authMode) || (param.clientId != mqtt.clientId) ||
            (param.userName != mqtt.userName) || (param.passwd != mqtt.passwd)) {
            mqtt.bEnable  = param.enable;
            mqtt.ip       = param.url;
            mqtt.port     = param.port;
            mqtt.authMode = param.authMode;
            mqtt.clientId = param.clientId;
            mqtt.userName = param.userName;
            mqtt.passwd   = param.passwd;
            sys_config_state_->SaveCfg();
            changed = true;
        }
    }
    if (changed) {
        if (param.enable) {
            service::ServiceRegistry::Instance().Get<service::INetworkService>().MqttStop();
            service::ServiceRegistry::Instance().Get<service::INetworkService>().MqttStart();
        } else {
            service::ServiceRegistry::Instance().Get<service::INetworkService>().MqttStop();
        }
    }
    return {};
}

cosmo::RunMode SystemServiceImpl::GetRunMode() {
    std::shared_lock<std::shared_mutex> lock(sys_config_state_->mtx);
    return sys_config_state_->config.interfaceModeInfo.runMode;
}

void SystemServiceImpl::SetRunMode(cosmo::RunMode mode) {
    bool changed = false;
    {
        std::lock_guard<std::shared_mutex> lock(sys_config_state_->mtx);
        if (mode != sys_config_state_->config.interfaceModeInfo.runMode) {
            sys_config_state_->config.interfaceModeInfo.runMode = mode;
            sys_config_state_->SaveCfg();
            changed = true;
        }
    }
    if (changed) {
        LOG_WARN("{}", "RunMode changed, rebooting device (face libraries will be cleared)");
        ServiceRegistry::Instance().Get<ISystemOperationService>().RebootDevice("RunModel Changed");
    }
}

IotNetworkParam SystemServiceImpl::GetIotNetworkParam() {
    IotNetworkParam result;
    {
        std::shared_lock<std::shared_mutex> lock(sys_config_state_->mtx);
        auto& param     = sys_config_state_->config.interfaceModeInfo.iotParam;
        result.httpUrl  = param.httpParam.url;
        result.mqttIp   = param.mqttParam.ip;
        result.mqttPort = param.mqttParam.port;
    }
    result.status = service::ServiceRegistry::Instance().Get<service::INetworkService>().IsMqttEnabled();
    return result;
}

void SystemServiceImpl::SetIotNetworkParam(const std::string& httpUrl, const std::string& mqttIp,
                                           int mqtt_port) {
    std::lock_guard<std::shared_mutex> lock(sys_config_state_->mtx);
    auto& iot = sys_config_state_->config.interfaceModeInfo.iotParam;
    if ((httpUrl != iot.httpParam.url) || (mqttIp != iot.mqttParam.ip) || (mqtt_port != iot.mqttParam.port)) {
        iot.httpParam.url     = httpUrl;
        iot.httpParam.bEnable = true;
        iot.mqttParam.ip      = mqttIp;
        iot.mqttParam.port    = mqtt_port;
        iot.mqttParam.bEnable = true;
        sys_config_state_->SaveCfg();
    }
}

bool SystemServiceImpl::GetResourceLimit() {
    std::shared_lock<std::shared_mutex> lock(sys_config_state_->mtx);
    return sys_config_state_->config.resourceInfo.bResourceLimitOpen;
}

void SystemServiceImpl::SetResourceLimit(bool enable) {
    std::lock_guard<std::shared_mutex> lock(sys_config_state_->mtx);
    if (enable != sys_config_state_->config.resourceInfo.bResourceLimitOpen) {
        sys_config_state_->config.resourceInfo.bResourceLimitOpen = enable;
        sys_config_state_->SaveCfg();
    }
}

bool SystemServiceImpl::IsNetworkModel() {
    std::shared_lock<std::shared_mutex> lock(sys_config_state_->mtx);
    return (cosmo::RunMode::RunModeIotNetwork == sys_config_state_->config.interfaceModeInfo.runMode);
}

bool SystemServiceImpl::GetActionSwitch(const std::string& actionId) {
    std::shared_lock<std::shared_mutex> lock(sys_config_state_->mtx);
    if (!sys_config_state_->tmp.debugInfo.bDebugOpen) {
        return true;
    }
    auto it = std::find_if(sys_config_state_->tmp.debugInfo.shieldActions.begin(),
                           sys_config_state_->tmp.debugInfo.shieldActions.end(),
                           [&](const std::string& action) { return action == actionId; });
    return (it == sys_config_state_->tmp.debugInfo.shieldActions.end());
}

}  // namespace cosmo::service
