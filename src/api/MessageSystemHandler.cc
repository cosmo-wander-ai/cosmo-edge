// MessageSystemHandler — System message handler.

#include "api/MessageSystemHandler.h"

#include "service/system/IConfigReadService.h"
#include "service/system/IConfigWriteService.h"
#include "service/system/IDeviceInfoService.h"
#include "service/system/ISystemOperationService.h"
#include "service/system/ITimeService.h"
#include "util/CipherUtil.h"
#include "util/ErrorCode.h"
#include "util/FileUtil.h"
#include "util/FormatString.h"
#include "util/Log.h"
#include "util/UuidUtil.h"

namespace cosmo {

namespace {
    // NTP parameter limits
    constexpr int kNtpIntervalMin = 1;
    constexpr int kNtpIntervalMax = 1440;
    constexpr int kNtpEnableMin   = 0;
    constexpr int kNtpEnableMax   = 2;
    constexpr int kTimeZoneIdMin  = 1;
    constexpr int kTimeZoneIdMax  = 95;

    // Reboot/Reset messages
    constexpr const char* kRebootMsg = "Rebooting, please do not power off";
    constexpr const char* kResetMsg  = "Resetting, please do not power off";
    constexpr int kRebootWaitSec     = 40;
}  // namespace

MessageSystemHandler::MessageSystemHandler(service::IConfigReadService& config_read,
                                           service::IConfigWriteService& config_write,
                                           service::IConfigNetworkService& config_network,
                                           service::IDeviceInfoService& device_info,
                                           service::ISystemOperationService& system_op,
                                           service::ITimeService& time_service)
    : config_read_(config_read),
      config_write_(config_write),
      config_network_(config_network),
      device_info_(device_info),
      system_op_(system_op),
      time_service_(time_service) {}

// Device information
System::MsgQueryDeviceInfoSend MessageSystemHandler::Handle(System::MsgQueryDeviceInfoRecv&& /*data*/,
                                                            std::error_condition& /*errc*/) {
    System::MsgQueryDeviceInfoSend retData{};
    auto info = device_info_.GetDeviceInfo();
    retData.resData.devInfoList.push_back({"deviceType", "设备型号", info.devModel});
    retData.resData.devInfoList.push_back({"hardwareVersion", "固件版本", info.devVersion});
    retData.resData.devInfoList.push_back({"softwareVersion", "软件版本", info.softwareVersion});
    retData.resData.devInfoList.push_back({"deviceSn", "设备SN", info.devSn});
    retData.resData.devInfoList.push_back({"devRunTimeSec", "运行时间", std::to_string(info.appRuntime)});
    return retData;
}

// Hardware resources
System::MsgQueryHardwareResourceSend MessageSystemHandler::Handle(
    System::MsgQueryHardwareResourceRecv&& /*data*/, std::error_condition& /*errc*/) {
    System::MsgQueryHardwareResourceSend retData{};
    double customScore = 0.0;
    auto items         = device_info_.GetHardwareResource(customScore);
    for (auto& item : items) {
        System::MsgQueryHardwareResourceSend::Item it{};
        it.key         = item.key;
        it.name        = item.name;
        it.usedPercent = item.usedPercent;
        it.usedSize    = item.usedSize;
        it.unusedSize  = item.unusedSize;
        it.available   = item.available;
        retData.resData.itemList.push_back(std::move(it));
    }
    retData.resData.customScore = COSMO_FORMAT("{:.4f}", customScore);
    return retData;
}

// Time query
System::MsgQueryTimeSend MessageSystemHandler::Handle(System::MsgQueryTimeRecv&& /*data*/,
                                                      std::error_condition& /*errc*/) {
    System::MsgQueryTimeSend retData{};
    std::vector<service::TimeZoneItem> zones;
    auto ts = time_service_.GetTimeStatus(zones);

    retData.resData.timeStatus.timestamp     = ts.timestamp;
    retData.resData.timeStatus.timeString    = ts.timeString;
    retData.resData.timeStatus.timeZoneValue = ts.timeZoneValue;
    retData.resData.timeStatus.timeZoneId    = ts.timeZoneId;
    retData.resData.timeStatus.ntp.enable    = ts.ntp.enable;
    retData.resData.timeStatus.ntp.server    = ts.ntp.server;
    retData.resData.timeStatus.ntp.interval  = ts.ntp.interval;
    retData.resData.timeStatus.ntp.port      = ts.ntp.port;

    for (auto& z : zones) {
        System::MsgQueryTimeSend::ZoneInfo zf{};
        zf.name  = z.name;
        zf.value = z.value;
        zf.id    = z.id;
        retData.resData.zoneInfoList.push_back(std::move(zf));
    }
    return retData;
}

// NTP sync
System::MsgNTPDateSend MessageSystemHandler::Handle(System::MsgNTPDateRecv&& data,
                                                    std::error_condition& errc) {
    System::MsgNTPDateSend retData{};
    if (data.interval < kNtpIntervalMin || data.interval > kNtpIntervalMax) {
        errc = util::ErrorEnum::ParameterException;
        return retData;
    }
    if (data.ntpEnable < kNtpEnableMin || data.ntpEnable > kNtpEnableMax) {
        errc = util::ErrorEnum::ParameterException;
        return retData;
    }
    if (data.timeZoneId < kTimeZoneIdMin || data.timeZoneId > kTimeZoneIdMax) {
        errc = util::ErrorEnum::ParameterException;
        return retData;
    }
    if ((1 == data.ntpEnable || 2 == data.ntpEnable) && data.ntpServer.empty()) {
        errc = util::ErrorEnum::ParameterException;
        return retData;
    }
    service::NtpConfig config{data.ntpEnable, data.ntpServer, data.ntpPort, data.interval};
    errc = time_service_.SyncNtp(config, data.timeZoneId);
    return retData;
}

// Time setting
System::MsgModifyTimeSend MessageSystemHandler::Handle(System::MsgModifyTimeRecv&& data,
                                                       std::error_condition& errc) {
    System::MsgModifyTimeSend retData{};
    if (data.timeZoneId < kTimeZoneIdMin || data.timeZoneId > kTimeZoneIdMax) {
        errc = util::ErrorEnum::ParameterException;
        return retData;
    }
    errc = time_service_.SetTime(data.timestamp, data.timeZoneId);
    return retData;
}

// Photo quality reset
System::MsgResetPictureQualitySend MessageSystemHandler::Handle(System::MsgResetPictureQualityRecv&& /*data*/,
                                                                std::error_condition& errc) {
    System::MsgResetPictureQualitySend retData{};
    errc = config_write_.ResetPictureQuality();
    return retData;
}

// Photo quality setting
System::MsgSetPictureQualitySend MessageSystemHandler::Handle(System::MsgSetPictureQualityRecv&& data,
                                                              std::error_condition& errc) {
    System::MsgSetPictureQualitySend retData{};
    CfgAlarmParamOverviewInfo info;
    info.picQuality         = data.fullPictureQuality;
    info.areaOverview       = data.areaOverlay;
    info.targetBoxOverview  = data.targetBoxOverlay;
    info.targetSizeOverview = data.targetSizeOverlay;
    info.alarmTypeOverview  = data.alarmNameOverlay;
    errc                    = config_write_.SetPictureQuality(info);
    return retData;
}

// Photo quality query
System::MsgQueryPictureQualitySend MessageSystemHandler::Handle(System::MsgQueryPictureQualityRecv&& /*data*/,
                                                                std::error_condition& /*errc*/) {
    System::MsgQueryPictureQualitySend retData{};
    auto info                          = config_read_.GetPictureQuality();
    retData.resData.fullPictureQuality = info.picQuality;
    retData.resData.areaOverlay        = info.areaOverview;
    retData.resData.targetBoxOverlay   = info.targetBoxOverview;
    retData.resData.targetSizeOverlay  = info.targetSizeOverview;
    retData.resData.alarmNameOverlay   = info.alarmTypeOverview;
    return retData;
}

// Alarm recording time reset
System::MsgResetAlarmVideoDurationSend MessageSystemHandler::Handle(
    System::MsgResetAlarmVideoDurationRecv&& /*data*/, std::error_condition& errc) {
    System::MsgResetAlarmVideoDurationSend retData{};
    errc = config_write_.ResetAlarmVideoDuration();
    return retData;
}

// Alarm recording time setting
System::MsgSetAlarmVideoDurationSend MessageSystemHandler::Handle(System::MsgSetAlarmVideoDurationRecv&& data,
                                                                  std::error_condition& errc) {
    System::MsgSetAlarmVideoDurationSend retData{};
    CfgAlarmParamVideoRecordInfo info;
    info.bopen         = data.enable;
    info.preDuration   = data.videoPreTime;
    info.aftreDuration = data.videoPostTime;
    errc               = config_write_.SetAlarmVideoDuration(info);
    return retData;
}

// Alarm recording time query
System::MsgQueryAlarmVideoDurationSend MessageSystemHandler::Handle(
    System::MsgQueryAlarmVideoDurationRecv&& /*data*/, std::error_condition& /*errc*/) {
    System::MsgQueryAlarmVideoDurationSend retData{};
    auto info                     = config_read_.GetAlarmVideoDuration();
    retData.resData.enable        = info.bopen;
    retData.resData.videoPreTime  = info.preDuration;
    retData.resData.videoPostTime = info.aftreDuration;
    return retData;
}

// Scheduled reboot reset
System::MsgResetDevRestartParamSend MessageSystemHandler::Handle(
    System::MsgResetDevRestartParamRecv&& /*data*/, std::error_condition& errc) {
    System::MsgResetDevRestartParamSend retData{};
    errc = config_write_.ResetRebootParam();
    return retData;
}

// Scheduled reboot setting
System::MsgModifyDevRestartParamSend MessageSystemHandler::Handle(System::MsgModifyDevRestartParamRecv&& data,
                                                                  std::error_condition& errc) {
    System::MsgModifyDevRestartParamSend retData{};
    CfgRebootParamInfo info;
    info.isTimingRestart = data.isTimingRestart;
    info.weekDay         = data.weekDay;

    if (data.restartTime.empty()) {
        LOG_WARN("{}", "[TimerRestart] Invalid empty restartTime");
        errc = util::ErrorEnum::ParameterException;
        return retData;
    }

    unsigned int h{0}, m{0};
    int ret = sscanf(data.restartTime.c_str(), "%02u:%02u", &h, &m);
    if ((ret != 2) || (h >= 24) || (m >= 60)) {
        LOG_WARN("[TimerRestart] Analysis invalid restartTime:{}", data.restartTime);
        errc = util::ErrorEnum::ParameterException;
        return retData;
    }
    info.restartTimeSec = h * 3600 + m * 60;
    errc                = config_write_.SetRebootParam(info);
    return retData;
}

// Scheduled reboot query
System::MsgQueryDevRestartParamSend MessageSystemHandler::Handle(
    System::MsgQueryDevRestartParamRecv&& /*data*/, std::error_condition& /*errc*/) {
    System::MsgQueryDevRestartParamSend retData{};
    auto info                       = config_read_.GetRebootParam();
    retData.resData.isTimingRestart = info.isTimingRestart;
    retData.resData.weekDay         = info.weekDay;
    retData.resData.restartTime =
        COSMO_FORMAT("{:02}:{:02}", info.restartTimeSec / 3600, (info.restartTimeSec % 3600) / 60);
    return retData;
}

// Reboot/Reset
System::MsgResetSystemSend MessageSystemHandler::Handle(System::MsgResetSystemRecv&& data,
                                                        std::error_condition& errc) {
    System::MsgResetSystemSend retData{};
    switch (data.resetOperation) {
        case 0: {
            retData.resData.locationUrl = "/";
            retData.resData.waitSeconds = kRebootWaitSec;
            retData.resultMsg           = kRebootMsg;
            system_op_.RebootDevice("Interface Reboot");
        } break;
        case 1: {
            retData.resData.locationUrl = "/";
            retData.resData.waitSeconds = kRebootWaitSec;
            retData.resultMsg           = kResetMsg;
            system_op_.ResetDevice("Interface Reset");
        } break;
        default: {
            errc = util::ErrorEnum::ParameterException;
        } break;
    }
    return retData;
}

// Config and log export
System::MsgExportFileSend MessageSystemHandler::Handle(System::MsgExportFileRecv&& data,
                                                       std::error_condition& errc) {
    System::MsgExportFileSend retData{};
    if (data.exportType != 1) {
        throw util::ErrorMessage(util::ErrorEnum::OperationNotSupport);
    }
    errc = system_op_.ExportLogs(retData.resData.fileName, retData.resData.fileUrl);
    return retData;
}

// Upgrade
System::MsgUpgradeSend MessageSystemHandler::Handle(System::MsgUpgradeRecv&& data,
                                                    std::error_condition& errc) {
    System::MsgUpgradeSend retData{};
    if (data.filePath.empty() || !util::FileExist(data.filePath) || util::GetFileSize(data.filePath) == 0) {
        LOG_WARN("upgrade upload file invalid, fileName={}, filePath={}, contentLength={}", data.fileName,
                 data.filePath, data.contentLength);
        errc = util::ErrorEnum::UpLoadDataEmpty;
        return retData;
    }
    errc = system_op_.Upgrade(data.filePath);
    return retData;
}

}  // namespace cosmo
