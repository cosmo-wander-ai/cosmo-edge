// SystemServiceImpl — System Service Impl implementation.

#include "service/system/impl/SystemServiceImpl.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <limits>
#include <mutex>
#include <nlohmann/json.hpp>
#include <shared_mutex>
#include <string_view>
#include <utility>

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
#include "util/Exec.h"
#include "util/FileUtil.h"
#include "util/HttpUrlUtil.h"
#include "util/JsonStructUtil.h"
#include "util/LimitedTypeJson.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/StringUtil.h"
#include "util/UuidUtil.h"

namespace cosmo::service {

namespace {

    constexpr size_t kMaxLogoBytes           = 1024 * 1024;
    constexpr size_t kMaxMqttHostBytes       = 253;
    constexpr size_t kMaxMqttIdentityBytes   = 128;
    constexpr size_t kMaxMqttCredentialBytes = 256;
    constexpr size_t kMaxShieldedActions     = 256;
    constexpr size_t kMaxActionIdBytes       = 128;

    class ScopedFd {
    public:
        explicit ScopedFd(int fd = -1) noexcept : fd_(fd) {}
        ~ScopedFd() {
            if (fd_ >= 0) {
                close(fd_);
            }
        }
        ScopedFd(const ScopedFd&)            = delete;
        ScopedFd& operator=(const ScopedFd&) = delete;

        int Get() const noexcept {
            return fd_;
        }

    private:
        int fd_;
    };

    bool IsAllowedLogoName(const std::string& name) {
        return name == "logo.png" || name == "logo.jpg" || name == "logo.jpeg";
    }

    bool ContainsControlCharacter(std::string_view value) {
        return std::any_of(value.begin(), value.end(), [](char character) {
            const auto byte = static_cast<unsigned char>(character);
            return byte < 0x20 || byte == 0x7f;
        });
    }

    bool IsValidMqttHost(const std::string& host) {
        return !host.empty() && host.size() <= kMaxMqttHostBytes && !ContainsControlCharacter(host) &&
               cosmo::util::IsHostnameSafe(host) && std::any_of(host.begin(), host.end(), [](char character) {
                   return std::isalnum(static_cast<unsigned char>(character)) != 0;
               });
    }

    bool IsValidMqttParam(const MqttParam& param) {
        if (param.port < 1 || param.port > 65535 || param.authMode < 0 || param.authMode > 1 ||
            (!param.url.empty() && !IsValidMqttHost(param.url)) ||
            param.clientId.size() > kMaxMqttIdentityBytes ||
            param.userName.size() > kMaxMqttCredentialBytes ||
            param.passwd.size() > kMaxMqttCredentialBytes || ContainsControlCharacter(param.clientId) ||
            ContainsControlCharacter(param.userName) || ContainsControlCharacter(param.passwd)) {
            return false;
        }
        if (param.enable && !IsValidMqttHost(param.url)) {
            return false;
        }
        return !param.enable || param.authMode == 0 ||
               (!param.clientId.empty() && !param.userName.empty() && !param.passwd.empty());
    }

    bool IsValidShieldedActions(const std::vector<std::string>& actions) {
        return actions.size() <= kMaxShieldedActions &&
               std::all_of(actions.begin(), actions.end(), [](const std::string& action) {
                   return !action.empty() && action.size() <= kMaxActionIdBytes &&
                          !ContainsControlCharacter(action);
               });
    }

    bool LogoContentMatches(const std::string& extension, const std::vector<uint8_t>& image) {
        if (extension == ".png") {
            constexpr uint8_t kPngSignature[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
            return image.size() >= sizeof(kPngSignature) &&
                   std::equal(std::begin(kPngSignature), std::end(kPngSignature), image.begin());
        }
        if (extension == ".jpg" || extension == ".jpeg") {
            return image.size() >= 3 && image[0] == 0xff && image[1] == 0xd8 && image[2] == 0xff;
        }
        return false;
    }

    bool IsValidBrandingText(const std::string& value) {
        if (ContainsControlCharacter(value)) {
            return false;
        }
        try {
            return cosmo::util::UTF8Length(value) <= 64;
        } catch (const std::exception&) {
            return false;
        }
    }

    bool IsValidStoredMqtt(const cosmo::MqttUnitParam& stored) {
        MqttParam param;
        param.enable   = stored.bEnable;
        param.url      = stored.ip;
        param.port     = stored.port;
        param.authMode = stored.authMode;
        param.clientId = stored.clientId;
        param.userName = stored.userName;
        param.passwd   = stored.passwd;
        return IsValidMqttParam(param);
    }

    bool IsValidStoredIot(const cosmo::CfgSystemInterfaceModeIotInfo& stored) {
        const bool mqtt_valid = stored.mqttParam.port >= 1 && stored.mqttParam.port <= 65535 &&
                                ((!stored.mqttParam.bEnable && stored.mqttParam.ip.empty()) ||
                                 IsValidMqttHost(stored.mqttParam.ip));
        const bool http_valid = (!stored.httpParam.bEnable && stored.httpParam.url.empty()) ||
                                cosmo::util::IsValidHttpUrl(stored.httpParam.url);
        return mqtt_valid && http_valid;
    }

    class LogoFileTransaction {
    public:
        LogoFileTransaction(const std::string& directory, std::string file_name,
                            const std::vector<uint8_t>& image)
            : directory_(directory),
              file_name_(std::move(file_name)),
              directory_fd_(open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)) {
            if (directory_fd_.Get() < 0) {
                return;
            }

            temporary_name_ = ".logo-" + cosmo::util::GenerateUUID() + ".tmp";
            ScopedFd temporary_fd(openat(directory_fd_.Get(), temporary_name_.c_str(),
                                         O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
            if (temporary_fd.Get() < 0) {
                return;
            }

            size_t offset = 0;
            while (offset < image.size()) {
                const auto remaining =
                    std::min(image.size() - offset, static_cast<size_t>(std::numeric_limits<ssize_t>::max()));
                const auto written = write(temporary_fd.Get(), image.data() + offset, remaining);
                if (written < 0 && errno == EINTR) {
                    continue;
                }
                if (written <= 0) {
                    return;
                }
                offset += static_cast<size_t>(written);
            }
            prepared_ = fchmod(temporary_fd.Get(), 0644) == 0 && fsync(temporary_fd.Get()) == 0;
        }

        ~LogoFileTransaction() {
            if (committed_ || directory_fd_.Get() < 0) {
                return;
            }
            if (installed_) {
                unlinkat(directory_fd_.Get(), file_name_.c_str(), 0);
            }
            if (!backup_name_.empty()) {
                renameat(directory_fd_.Get(), backup_name_.c_str(), directory_fd_.Get(), file_name_.c_str());
            }
            if (!temporary_name_.empty()) {
                unlinkat(directory_fd_.Get(), temporary_name_.c_str(), 0);
            }
            static_cast<void>(fsync(directory_fd_.Get()));
        }

        LogoFileTransaction(const LogoFileTransaction&)            = delete;
        LogoFileTransaction& operator=(const LogoFileTransaction&) = delete;

        bool Install() {
            if (!prepared_) {
                return false;
            }

            struct stat status {};
            if (fstatat(directory_fd_.Get(), file_name_.c_str(), &status, AT_SYMLINK_NOFOLLOW) == 0) {
                if (!S_ISREG(status.st_mode)) {
                    return false;
                }
                backup_name_ = ".logo-backup-" + cosmo::util::GenerateUUID() + ".tmp";
                if (renameat(directory_fd_.Get(), file_name_.c_str(), directory_fd_.Get(),
                             backup_name_.c_str()) != 0) {
                    backup_name_.clear();
                    return false;
                }
            } else if (errno != ENOENT) {
                return false;
            }

            if (renameat(directory_fd_.Get(), temporary_name_.c_str(), directory_fd_.Get(),
                         file_name_.c_str()) != 0) {
                if (!backup_name_.empty()) {
                    if (renameat(directory_fd_.Get(), backup_name_.c_str(), directory_fd_.Get(),
                                 file_name_.c_str()) == 0) {
                        backup_name_.clear();
                    }
                }
                return false;
            }
            temporary_name_.clear();
            installed_ = true;
            if (fsync(directory_fd_.Get()) != 0) {
                LOG_WARN("Failed to sync logo directory {}: {}", directory_, strerror(errno));
            }
            return true;
        }

        void Commit() {
            if (!backup_name_.empty()) {
                unlinkat(directory_fd_.Get(), backup_name_.c_str(), 0);
                backup_name_.clear();
            }
            if (fsync(directory_fd_.Get()) != 0) {
                LOG_WARN("Failed to sync logo directory {}: {}", directory_, strerror(errno));
            }
            committed_ = true;
        }

    private:
        std::string directory_;
        std::string file_name_;
        ScopedFd directory_fd_;
        std::string temporary_name_;
        std::string backup_name_;
        bool prepared_{false};
        bool installed_{false};
        bool committed_{false};
    };

}  // namespace

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
        cosmo::CfgSystemParamInfo loaded;
        if (cosmo::util::LoadStructFromJsonFile(cfg_path, loaded)) {
            if ((loaded.popUpInfo.popUpSwitch != 0 && loaded.popUpInfo.popUpSwitch != 1) ||
                (loaded.popUpInfo.audioPlay != 0 && loaded.popUpInfo.audioPlay != 1) ||
                loaded.popUpInfo.popUpDuration < 1 || loaded.popUpInfo.popUpDuration > 30) {
                LOG_WARN("{}", "Reject invalid persisted popup configuration");
                loaded.popUpInfo = cosmo::CfgSystemPopUpInfo{};
            }
            if (!IsValidStoredMqtt(loaded.interfaceModeInfo.standAloneParam.mqttParam)) {
                LOG_WARN("{}", "Reject invalid persisted standalone MQTT configuration");
                loaded.interfaceModeInfo.standAloneParam.mqttParam = cosmo::MqttUnitParam{};
            }
            if (!IsValidStoredIot(loaded.interfaceModeInfo.iotParam)) {
                LOG_WARN("{}", "Reject invalid persisted IoT endpoint configuration");
                loaded.interfaceModeInfo.iotParam = cosmo::CfgSystemInterfaceModeIotInfo{};
            }
            if (!IsValidBrandingText(loaded.logoInfo.systemName) ||
                !IsValidBrandingText(loaded.logoInfo.bigScreenName) ||
                (!loaded.logoInfo.logoFileName.empty() && !IsAllowedLogoName(loaded.logoInfo.logoFileName))) {
                LOG_WARN("{}", "Reject invalid persisted logo configuration");
                loaded.logoInfo = cosmo::CfgSystemLogoInfo{};
            }
            config = std::move(loaded);
        }
        LOG_INFO("{}", "SysConfigState: system config loaded");
    }

    bool SaveCfg(const cosmo::CfgSystemParamInfo& cfg) {
        auto path = (std::filesystem::path(cosmo::path::GetCfgPath()) / conf_file_name).string();
        return cosmo::util::SaveStructToJsonFile(path, cfg);
    }
};

SystemServiceImpl::SystemServiceImpl() : sys_config_state_(std::make_unique<SysConfigState>()) {
    auto alarm_cfg_path = (std::filesystem::path(cosmo::path::GetCfgPath()) / alarm_cfg_file_).string();
    CfgAlarmParamInfo alarm_config;
    if (cosmo::util::LoadStructFromJsonFile(alarm_cfg_path, alarm_config)) {
        if (alarm_config.overviewInfo.picQuality < 1 || alarm_config.overviewInfo.picQuality > 100) {
            LOG_WARN("{}", "Reject invalid persisted picture quality configuration");
            alarm_config.overviewInfo = cosmo::CfgAlarmParamOverviewInfo{};
        }
        if (alarm_config.videoRecordInfo.preDuration < 1 || alarm_config.videoRecordInfo.preDuration > 100 ||
            alarm_config.videoRecordInfo.aftreDuration < 1 ||
            alarm_config.videoRecordInfo.aftreDuration > 100) {
            LOG_WARN("{}", "Reject invalid persisted alarm recording configuration");
            alarm_config.videoRecordInfo = cosmo::CfgAlarmParamVideoRecordInfo{};
        }
        overview_config_     = alarm_config.overviewInfo;
        video_record_config_ = alarm_config.videoRecordInfo;
    }
    LOG_INFO("{}", "SystemServiceImpl: alarm config loaded");

    auto reboot_cfg_path = (std::filesystem::path(cosmo::path::GetCfgPath()) / reboot_cfg_file_).string();
    cosmo::CfgRebootParamInfo reboot_config;
    if (cosmo::util::LoadStructFromJsonFile(reboot_cfg_path, reboot_config) && reboot_config.weekDay >= 0 &&
        reboot_config.weekDay <= 7 && reboot_config.restartTimeSec >= 0 &&
        reboot_config.restartTimeSec < 24 * 60 * 60) {
        reboot_config_ = reboot_config;
    } else {
        LOG_WARN("{}", "Reject invalid persisted reboot configuration");
    }
    LOG_INFO("{}", "SystemServiceImpl: reboot config loaded");
}

SystemServiceImpl::~SystemServiceImpl() = default;

bool SystemServiceImpl::SaveAlarmCfg(const CfgAlarmParamInfo& cfg) {
    auto path = (std::filesystem::path(cosmo::path::GetCfgPath()) / alarm_cfg_file_).string();
    return cosmo::util::SaveStructToJsonFile(path, cfg);
}

bool SystemServiceImpl::SaveRebootCfg(const cosmo::CfgRebootParamInfo& cfg) {
    auto path = (std::filesystem::path(cosmo::path::GetCfgPath()) / reboot_cfg_file_).string();
    return cosmo::util::SaveStructToJsonFile(path, cfg);
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
    if ((info.picQuality == overview_config_.picQuality) &&
        (info.alarmTypeOverview == overview_config_.alarmTypeOverview) &&
        (info.areaOverview == overview_config_.areaOverview) &&
        (info.targetBoxOverview == overview_config_.targetBoxOverview) &&
        (info.targetSizeOverview == overview_config_.targetSizeOverview)) {
        return cosmo::util::ErrorEnum::Success;
    }
    CfgAlarmParamInfo snapshot{info, video_record_config_};
    if (!SaveAlarmCfg(snapshot)) {
        return cosmo::util::ErrorEnum::SysErr;
    }
    overview_config_ = std::move(info);
    return cosmo::util::ErrorEnum::Success;
}

cosmo::util::ErrorEnum SystemServiceImpl::ResetPictureQuality() {
    std::lock_guard<std::shared_mutex> lock(alarm_mtx_);
    CfgAlarmParamInfo snapshot{cosmo::CfgAlarmParamOverviewInfo{}, video_record_config_};
    if (!SaveAlarmCfg(snapshot)) {
        return cosmo::util::ErrorEnum::SysErr;
    }
    overview_config_ = snapshot.overviewInfo;
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
    if ((info.bopen == video_record_config_.bopen) &&
        (info.preDuration == video_record_config_.preDuration) &&
        (info.aftreDuration == video_record_config_.aftreDuration)) {
        return cosmo::util::ErrorEnum::Success;
    }
    CfgAlarmParamInfo snapshot{overview_config_, info};
    if (!SaveAlarmCfg(snapshot)) {
        return cosmo::util::ErrorEnum::SysErr;
    }
    video_record_config_ = std::move(info);
    return cosmo::util::ErrorEnum::Success;
}

cosmo::util::ErrorEnum SystemServiceImpl::ResetAlarmVideoDuration() {
    std::lock_guard<std::shared_mutex> lock(alarm_mtx_);
    CfgAlarmParamInfo snapshot{overview_config_, cosmo::CfgAlarmParamVideoRecordInfo{}};
    if (!SaveAlarmCfg(snapshot)) {
        return cosmo::util::ErrorEnum::SysErr;
    }
    video_record_config_ = snapshot.videoRecordInfo;
    return cosmo::util::ErrorEnum::Success;
}

cosmo::CfgRebootParamInfo SystemServiceImpl::GetRebootParam() {
    std::shared_lock<std::shared_mutex> lock(reboot_mtx_);
    return reboot_config_;
}

cosmo::util::ErrorEnum SystemServiceImpl::SetRebootParam(cosmo::CfgRebootParamInfo info) {
    if ((info.weekDay < 0) || (info.weekDay > 7) || info.restartTimeSec < 0 ||
        info.restartTimeSec >= 24 * 60 * 60) {
        return cosmo::util::ErrorEnum::ParameterException;
    }
    std::lock_guard<std::shared_mutex> lock(reboot_mtx_);
    if ((info.isTimingRestart == reboot_config_.isTimingRestart) &&
        (info.weekDay == reboot_config_.weekDay) && (info.restartTimeSec == reboot_config_.restartTimeSec)) {
        return cosmo::util::ErrorEnum::Success;
    }
    if (!SaveRebootCfg(info)) {
        return cosmo::util::ErrorEnum::SysErr;
    }
    reboot_config_ = std::move(info);
    return cosmo::util::ErrorEnum::Success;
}

cosmo::util::ErrorEnum SystemServiceImpl::ResetRebootParam() {
    std::lock_guard<std::shared_mutex> lock(reboot_mtx_);
    cosmo::CfgRebootParamInfo snapshot;
    if (!SaveRebootCfg(snapshot)) {
        return cosmo::util::ErrorEnum::SysErr;
    }
    reboot_config_ = snapshot;
    return cosmo::util::ErrorEnum::Success;
}

SystemLogoInfo SystemServiceImpl::GetSystemLogo() {
    SystemLogoInfo info;
    std::shared_lock<std::shared_mutex> lock(sys_config_state_->mtx);
    if (sys_config_state_->config.logoInfo.bHaveSetLogo &&
        IsAllowedLogoName(sys_config_state_->config.logoInfo.logoFileName)) {
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
    if (!IsValidBrandingText(systemName) || !IsValidBrandingText(bigScreenName) || logo_img.empty() ||
        logo_img.size() > kMaxLogoBytes || !LogoContentMatches(logoFileType, logo_img)) {
        return cosmo::util::ErrorEnum::InvalidParam;
    }

    const std::string logo_name = "logo" + logoFileType;
    const std::string logo_dir  = cosmo::path::GetLogoPath();
    if (!IsAllowedLogoName(logo_name)) {
        return cosmo::util::ErrorEnum::InvalidParam;
    }

    std::lock_guard<std::shared_mutex> lock(sys_config_state_->mtx);
    LogoFileTransaction logo_transaction(logo_dir, logo_name, logo_img);
    if (!logo_transaction.Install()) {
        return cosmo::util::ErrorEnum::SysErr;
    }

    auto snapshot                  = sys_config_state_->config;
    const auto old_logo_name       = snapshot.logoInfo.logoFileName;
    snapshot.logoInfo.systemName   = systemName;
    snapshot.logoInfo.logoFileName = logo_name;
    snapshot.logoInfo.bHaveSetLogo = true;
    if (!bigScreenName.empty()) {
        snapshot.logoInfo.bigScreenName = bigScreenName;
    }
    if (!sys_config_state_->SaveCfg(snapshot)) {
        return cosmo::util::ErrorEnum::SysErr;
    }
    sys_config_state_->config = std::move(snapshot);
    logo_transaction.Commit();

    if (old_logo_name != logo_name && IsAllowedLogoName(old_logo_name)) {
        std::error_code remove_error;
        std::filesystem::remove(std::filesystem::path(logo_dir) / old_logo_name, remove_error);
    }
    return cosmo::util::ErrorEnum::Success;
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

cosmo::util::ErrorEnum SystemServiceImpl::SetShieldedActions(const std::vector<std::string>& actions) {
    if (!IsValidShieldedActions(actions)) {
        return cosmo::util::ErrorEnum::InvalidParam;
    }
    std::lock_guard<std::shared_mutex> lock(sys_config_state_->mtx);
    sys_config_state_->tmp.debugInfo.shieldActions = actions;
    return cosmo::util::ErrorEnum::Success;
}

void SystemServiceImpl::GetPopUpParam(int& pop_up_switch, int& audio_play, int& pop_up_duration) {
    std::shared_lock<std::shared_mutex> lock(sys_config_state_->mtx);
    pop_up_switch   = sys_config_state_->config.popUpInfo.popUpSwitch;
    audio_play      = sys_config_state_->config.popUpInfo.audioPlay;
    pop_up_duration = sys_config_state_->config.popUpInfo.popUpDuration;
}

cosmo::util::ErrorEnum SystemServiceImpl::SetPopUpParam(int pop_up_switch, int audio_play,
                                                        int pop_up_duration) {
    if ((pop_up_switch != 0 && pop_up_switch != 1) || (audio_play != 0 && audio_play != 1) ||
        pop_up_duration < 1 || pop_up_duration > 30) {
        return cosmo::util::ErrorEnum::InvalidParam;
    }
    std::lock_guard<std::shared_mutex> lock(sys_config_state_->mtx);
    if ((pop_up_switch == sys_config_state_->config.popUpInfo.popUpSwitch) &&
        (pop_up_duration == sys_config_state_->config.popUpInfo.popUpDuration) &&
        (audio_play == sys_config_state_->config.popUpInfo.audioPlay)) {
        return cosmo::util::ErrorEnum::Success;
    }
    auto snapshot                    = sys_config_state_->config;
    snapshot.popUpInfo.popUpSwitch   = pop_up_switch;
    snapshot.popUpInfo.popUpDuration = pop_up_duration;
    snapshot.popUpInfo.audioPlay     = audio_play;
    if (!sys_config_state_->SaveCfg(snapshot)) {
        return cosmo::util::ErrorEnum::SysErr;
    }
    sys_config_state_->config = std::move(snapshot);
    return cosmo::util::ErrorEnum::Success;
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
    if (!IsValidMqttParam(param)) {
        return cosmo::util::ErrorEnum::InvalidParam;
    }
    bool changed = false;
    {
        std::lock_guard<std::shared_mutex> lock(sys_config_state_->mtx);
        const auto& mqtt = sys_config_state_->config.interfaceModeInfo.standAloneParam.mqttParam;
        if ((param.enable != mqtt.bEnable) || (param.url != mqtt.ip) || (param.port != mqtt.port) ||
            (param.authMode != mqtt.authMode) || (param.clientId != mqtt.clientId) ||
            (param.userName != mqtt.userName) || (param.passwd != mqtt.passwd)) {
            auto snapshot      = sys_config_state_->config;
            auto& candidate    = snapshot.interfaceModeInfo.standAloneParam.mqttParam;
            candidate.bEnable  = param.enable;
            candidate.ip       = param.url;
            candidate.port     = param.port;
            candidate.authMode = param.authMode;
            candidate.clientId = param.clientId;
            candidate.userName = param.userName;
            candidate.passwd   = param.passwd;
            if (!sys_config_state_->SaveCfg(snapshot)) {
                return cosmo::util::ErrorEnum::SysErr;
            }
            sys_config_state_->config = std::move(snapshot);
            changed                   = true;
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
    return cosmo::util::ErrorEnum::Success;
}

cosmo::RunMode SystemServiceImpl::GetRunMode() {
    std::shared_lock<std::shared_mutex> lock(sys_config_state_->mtx);
    return sys_config_state_->config.interfaceModeInfo.runMode;
}

cosmo::util::ErrorEnum SystemServiceImpl::SetRunMode(cosmo::RunMode mode) {
    if (mode != cosmo::RunMode::RunModeStandAlone && mode != cosmo::RunMode::RunModeIotNetwork) {
        return cosmo::util::ErrorEnum::InvalidParam;
    }
    bool changed = false;
    {
        std::lock_guard<std::shared_mutex> lock(sys_config_state_->mtx);
        if (mode != sys_config_state_->config.interfaceModeInfo.runMode) {
            auto snapshot                      = sys_config_state_->config;
            snapshot.interfaceModeInfo.runMode = mode;
            if (!sys_config_state_->SaveCfg(snapshot)) {
                return cosmo::util::ErrorEnum::SysErr;
            }
            sys_config_state_->config = std::move(snapshot);
            changed                   = true;
        }
    }
    if (changed) {
        LOG_WARN("{}", "RunMode changed, rebooting device (face libraries will be cleared)");
        ServiceRegistry::Instance().Get<ISystemOperationService>().RebootDevice("RunModel Changed");
    }
    return cosmo::util::ErrorEnum::Success;
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

cosmo::util::ErrorEnum SystemServiceImpl::SetIotNetworkParam(const std::string& httpUrl,
                                                             const std::string& mqttIp, int mqtt_port) {
    if (!cosmo::util::IsValidHttpUrl(httpUrl) || !IsValidMqttHost(mqttIp) || mqtt_port < 1 ||
        mqtt_port > 65535) {
        return cosmo::util::ErrorEnum::InvalidParam;
    }
    std::lock_guard<std::shared_mutex> lock(sys_config_state_->mtx);
    const auto& iot = sys_config_state_->config.interfaceModeInfo.iotParam;
    if ((httpUrl == iot.httpParam.url) && (mqttIp == iot.mqttParam.ip) && (mqtt_port == iot.mqttParam.port) &&
        iot.httpParam.bEnable && iot.mqttParam.bEnable) {
        return cosmo::util::ErrorEnum::Success;
    }
    auto snapshot               = sys_config_state_->config;
    auto& candidate             = snapshot.interfaceModeInfo.iotParam;
    candidate.httpParam.url     = httpUrl;
    candidate.httpParam.bEnable = true;
    candidate.mqttParam.ip      = mqttIp;
    candidate.mqttParam.port    = mqtt_port;
    candidate.mqttParam.bEnable = true;
    if (!sys_config_state_->SaveCfg(snapshot)) {
        return cosmo::util::ErrorEnum::SysErr;
    }
    sys_config_state_->config = std::move(snapshot);
    return cosmo::util::ErrorEnum::Success;
}

bool SystemServiceImpl::GetResourceLimit() {
    std::shared_lock<std::shared_mutex> lock(sys_config_state_->mtx);
    return sys_config_state_->config.resourceInfo.bResourceLimitOpen;
}

cosmo::util::ErrorEnum SystemServiceImpl::SetResourceLimit(bool enable) {
    std::lock_guard<std::shared_mutex> lock(sys_config_state_->mtx);
    if (enable == sys_config_state_->config.resourceInfo.bResourceLimitOpen) {
        return cosmo::util::ErrorEnum::Success;
    }
    auto snapshot                            = sys_config_state_->config;
    snapshot.resourceInfo.bResourceLimitOpen = enable;
    if (!sys_config_state_->SaveCfg(snapshot)) {
        return cosmo::util::ErrorEnum::SysErr;
    }
    sys_config_state_->config = std::move(snapshot);
    return cosmo::util::ErrorEnum::Success;
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
