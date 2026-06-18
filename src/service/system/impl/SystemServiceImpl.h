#pragma once

#include <memory>
#include <shared_mutex>
#include <string>

#include "service/system/IConfigNetworkService.h"
#include "service/system/IConfigReadService.h"
#include "service/system/IConfigWriteService.h"

namespace cosmo::service {

class SystemServiceImpl : public IConfigReadService,
                          public IConfigWriteService,
                          public IConfigNetworkService {
public:
    SystemServiceImpl();
    ~SystemServiceImpl() override;

    // ---- IConfigReadService ----
    cosmo::CfgAlarmParamOverviewInfo GetPictureQuality() override;
    cosmo::CfgAlarmParamVideoRecordInfo GetAlarmVideoDuration() override;
    cosmo::CfgRebootParamInfo GetRebootParam() override;
    SystemLogoInfo GetSystemLogo() override;
    bool GetDebugMode() override;
    std::vector<std::string> GetShieldedActions() override;
    bool GetActionSwitch(const std::string& actionId) override;
    void GetPopUpParam(int& popUpSwitch, int& audioPlay, int& popUpDuration) override;
    cosmo::RunMode GetRunMode() override;
    bool GetResourceLimit() override;
    bool IsNetworkModel() override;

    // ---- IConfigWriteService ----
    cosmo::util::ErrorEnum SetPictureQuality(cosmo::CfgAlarmParamOverviewInfo info) override;
    cosmo::util::ErrorEnum ResetPictureQuality() override;
    cosmo::util::ErrorEnum SetAlarmVideoDuration(cosmo::CfgAlarmParamVideoRecordInfo info) override;
    cosmo::util::ErrorEnum ResetAlarmVideoDuration() override;
    cosmo::util::ErrorEnum SetRebootParam(cosmo::CfgRebootParamInfo info) override;
    cosmo::util::ErrorEnum ResetRebootParam() override;
    cosmo::util::ErrorEnum SetSystemLogo(const std::string& systemName, const std::string& logoFileType,
                                         const std::vector<uint8_t>& logoImg,
                                         const std::string& bigScreenName) override;
    void SetDebugMode(bool enable) override;
    void SetShieldedActions(const std::vector<std::string>& actions) override;
    void SetPopUpParam(int popUpSwitch, int audioPlay, int popUpDuration) override;
    void SetRunMode(cosmo::RunMode mode) override;
    void SetResourceLimit(bool enable) override;

    // ---- IConfigNetworkService ----
    HttpPushParam GetHttpInterfaceParam() override;
    cosmo::util::ErrorEnum SetHttpInterfaceParam(const HttpPushParam& param) override;
    MqttParam GetMqttParam() override;
    cosmo::util::ErrorEnum SetMqttParam(const MqttParam& param) override;
    IotNetworkParam GetIotNetworkParam() override;
    void SetIotNetworkParam(const std::string& httpUrl, const std::string& mqttIp, int mqttPort) override;

private:
    // Alarm config state (migrated from CfgAlarmParam singleton)
    mutable std::shared_mutex alarm_mtx_;
    std::string alarm_cfg_file_{"alarmParam.json"};
    cosmo::CfgAlarmParamOverviewInfo overview_config_;
    cosmo::CfgAlarmParamVideoRecordInfo video_record_config_;

    void SaveAlarmCfg();

    // Reboot config state (migrated from CfgRebootParam singleton)
    mutable std::shared_mutex reboot_mtx_;
    std::string reboot_cfg_file_{"devRebootParam.json"};
    cosmo::CfgRebootParamInfo reboot_config_;

    void SaveRebootCfg();

    // System config state (migrated from CfgSystemParam singleton)
    struct SysConfigState;
    std::unique_ptr<SysConfigState> sys_config_state_;
};

}  // namespace cosmo::service
