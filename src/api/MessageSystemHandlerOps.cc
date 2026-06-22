// MessageSystemHandlerOps.cc — Handler operations for MessageSystemHandler.
// Split from MessageSystemHandler.cc to reduce file size (DEBT-007).

#include <malloc.h>

#include "api/MessageSystemHandler.h"
#include "service/system/IConfigNetworkService.h"
#include "service/system/IConfigReadService.h"
#include "service/system/IConfigWriteService.h"
#include "service/system/IDeviceInfoService.h"
#include "service/system/ISystemOperationService.h"
#include "util/CipherUtil.h"
#include "util/ErrorCode.h"
#include "util/FormatString.h"
#include "util/Log.h"
#include "util/UuidUtil.h"
#include "util/VehicleDict.h"

namespace cosmo {

namespace {
    constexpr int kMinLogoSize = 100;  // Minimum valid logo image size in bytes
}  // namespace

// System logo query
System::MsgQuerySystemLogoSend MessageSystemHandler::Handle(System::MsgQuerySystemLogoRecv&& /*data*/,
                                                            std::error_condition& /*errc*/) {
    System::MsgQuerySystemLogoSend result{};
    auto logo                    = config_read_.GetSystemLogo();
    result.resData.systemName    = logo.systemName;
    result.resData.logoUrl       = logo.logoUrl;
    result.resData.bigScreenName = logo.bigScreenName;
    return result;
}

// System logo setting
System::MsgSetSystemLogoSend MessageSystemHandler::Handle(System::MsgSetSystemLogoRecv&& data,
                                                          std::error_condition& errc) {
    System::MsgSetSystemLogoSend result{};
    if (data.logoBase64.empty()) {
        errc = util::ErrorEnum::UpLoadDataEmpty;
        return result;
    }
    if (data.logoFileType.empty()) {
        errc = util::ErrorEnum::FileTypeParamEmpty;
        return result;
    }
    auto logoImg = util::DecBase64Vec(data.logoBase64);
    if (static_cast<int>(logoImg.size()) < kMinLogoSize) {
        errc = util::ErrorEnum::Failed;
        return result;
    }
    config_write_.SetSystemLogo(data.systemName, data.logoFileType, logoImg, data.bigScreenName);
    return result;
}

// Query device status
System::MsgQueryDeviceStatusSend MessageSystemHandler::Handle(System::MsgQueryDeviceStatusRecv&& /*data*/,
                                                              std::error_condition& errc) {
    System::MsgQueryDeviceStatusSend retData{};
    errc = util::ErrorEnum::Success;
    return retData;
}

// Debug mode setting
System::MsgModifyDebugModeSend MessageSystemHandler::Handle(System::MsgModifyDebugModeRecv&& data,
                                                            std::error_condition& /*errc*/) {
    System::MsgModifyDebugModeSend retData{};
    config_write_.SetDebugMode(data.debugModeOpen);
    return retData;
}

// Debug mode query
System::MsgQueryDebugModeSend MessageSystemHandler::Handle(System::MsgQueryDebugModeRecv&& /*data*/,
                                                           std::error_condition& /*errc*/) {
    System::MsgQueryDebugModeSend retData{};
    retData.resData.debugModeOpen = config_read_.GetDebugMode();
    return retData;
}

// Mask action setting
System::MsgModifyShiledActionsSend MessageSystemHandler::Handle(System::MsgModifyShiledActionsRecv&& data,
                                                                std::error_condition& /*errc*/) {
    System::MsgModifyShiledActionsSend retData{};
    config_write_.SetShieldedActions(data.shiledActions);
    return retData;
}

// Mask action query
System::MsgQueryShiledActionsSend MessageSystemHandler::Handle(System::MsgQueryShiledActionsRecv&& /*data*/,
                                                               std::error_condition& /*errc*/) {
    System::MsgQueryShiledActionsSend retData{};
    retData.resData.shiledActions = config_read_.GetShieldedActions();
    return retData;
}

// Thread debug
System::MsgThreadDebugInfoSend MessageSystemHandler::Handle(System::MsgThreadDebugInfoRecv&& /*data*/,
                                                            std::error_condition& /*errc*/) {
    System::MsgThreadDebugInfoSend retData{};
    system_op_.ShowThreadDebugInfo();
    return retData;
}

// Popup parameter query
System::MsgQueryPopUpParamSend MessageSystemHandler::Handle(System::MsgQueryPopUpParamRecv&& /*data*/,
                                                            std::error_condition& /*errc*/) {
    System::MsgQueryPopUpParamSend result{};
    config_read_.GetPopUpParam(result.resData.popUpSwitch, result.resData.audioPlay,
                               result.resData.popUpDuration);
    return result;
}

// Popup parameter setting
System::MsgSetPopUpParamSend MessageSystemHandler::Handle(System::MsgSetPopUpParamRecv&& data,
                                                          std::error_condition& /*errc*/) {
    System::MsgSetPopUpParamSend result{};
    config_write_.SetPopUpParam(data.popUpSwitch, data.audioPlay, data.popUpDuration);
    return result;
}

// HTTP API parameter query
System::MsgQueryHttpInterfaceParamSend MessageSystemHandler::Handle(
    System::MsgQueryHttpInterfaceParamRecv&& /*data*/, std::error_condition& /*errc*/) {
    System::MsgQueryHttpInterfaceParamSend result{};
    auto info             = config_network_.GetHttpInterfaceParam();
    result.resData.enable = info.enable;
    result.resData.url    = info.url;
    return result;
}

// HTTP API parameter setting
System::MsgSetHttpInterfaceParamSend MessageSystemHandler::Handle(System::MsgSetHttpInterfaceParamRecv&& data,
                                                                  std::error_condition& errc) {
    System::MsgSetHttpInterfaceParamSend result{};
    service::HttpPushParam param{data.enable, data.url};
    errc = config_network_.SetHttpInterfaceParam(param);
    return result;
}

// MQTT parameter query
System::MsgQueryMqttAdapterParamSend MessageSystemHandler::Handle(
    System::MsgQueryMqttAdapterParamRecv&& /*data*/, std::error_condition& /*errc*/) {
    System::MsgQueryMqttAdapterParamSend result{};
    auto info               = config_network_.GetMqttParam();
    result.resData.enable   = info.enable;
    result.resData.url      = info.url;
    result.resData.port     = info.port;
    result.resData.authMode = info.authMode;
    result.resData.clientId = info.clientId;
    result.resData.userName = info.userName;
    result.resData.passwd   = info.passwd;
    result.resData.status   = info.status;
    return result;
}

// MQTT parameter setting
System::MsgSetMqttAdapterParamSend MessageSystemHandler::Handle(System::MsgSetMqttAdapterParamRecv&& data,
                                                                std::error_condition& errc) {
    System::MsgSetMqttAdapterParamSend result{};
    service::MqttParam param;
    param.enable   = data.enable;
    param.url      = data.url;
    param.port     = data.port;
    param.authMode = data.authMode;
    param.clientId = data.clientId;
    param.userName = data.userName;
    param.passwd   = data.passwd;
    errc           = config_network_.SetMqttParam(param);
    return result;
}

// Running mode query
System::MsgQueryRunModeParamSend MessageSystemHandler::Handle(System::MsgQueryRunModeParamRecv&& /*data*/,
                                                              std::error_condition& /*errc*/) {
    System::MsgQueryRunModeParamSend result{};
    result.resData.runMode = config_read_.GetRunMode();
    return result;
}

// Running mode setting
System::MsgModifyRunModeParamSend MessageSystemHandler::Handle(System::MsgModifyRunModeParamRecv&& data,
                                                               std::error_condition& /*errc*/) {
    System::MsgModifyRunModeParamSend result{};
    config_write_.SetRunMode(data.runMode);
    return result;
}

// Network mode parameter query
System::MsgQueryIotNetworkParamSend MessageSystemHandler::Handle(
    System::MsgQueryIotNetworkParamRecv&& /*data*/, std::error_condition& /*errc*/) {
    System::MsgQueryIotNetworkParamSend result{};
    auto param              = config_network_.GetIotNetworkParam();
    result.resData.httpUrl  = param.httpUrl;
    result.resData.mqttIp   = param.mqttIp;
    result.resData.mqttPort = param.mqttPort;
    result.resData.status   = param.status;
    return result;
}

// Network mode parameter setting
System::MsgModifyIotNetworkParamSend MessageSystemHandler::Handle(System::MsgModifyIotNetworkParamRecv&& data,
                                                                  std::error_condition& /*errc*/) {
    System::MsgModifyIotNetworkParamSend result{};
    config_network_.SetIotNetworkParam(data.httpUrl, data.mqttIp, data.mqttPort);
    return result;
}

// Document URL query
System::MsgQueryDocumentUrlSend MessageSystemHandler::Handle(System::MsgQueryDocumentUrlRecv&& data,
                                                             std::error_condition& errc) {
    System::MsgQueryDocumentUrlSend result{};
    if (data.type == 0) {
        result.resData.url = "/staticfile/httpInterface.html";
    } else if (data.type == 1) {
        result.resData.url = "/staticfile/mqttInterface.html";
    } else {
        errc = util::ErrorEnum::InvalidParam;
    }
    return result;
}

// Resource limit query
System::MsgQueryResourceLimitParamSend MessageSystemHandler::Handle(
    System::MsgQueryResourceLimitParamRecv&& /*data*/, std::error_condition& /*errc*/) {
    System::MsgQueryResourceLimitParamSend result{};
    result.resData.enable = config_read_.GetResourceLimit();
    return result;
}

// Resource limit setting
System::MsgSetResourceLimitParamSend MessageSystemHandler::Handle(System::MsgSetResourceLimitParamRecv&& data,
                                                                  std::error_condition& /*errc*/) {
    System::MsgSetResourceLimitParamSend result{};
    config_write_.SetResourceLimit(data.enable);
    return result;
}

// Graceful exit
System::MsgDebugQuitSend MessageSystemHandler::Handle(System::MsgDebugQuitRecv&& /*data*/,
                                                      std::error_condition& errc) {
    System::MsgDebugQuitSend retData{};
    errc = util::ErrorEnum::DebugQuit;
    LOG_WARN("{}", "QUIT");
    return retData;
}

// Defragment memory
System::MsgDebugSystemMemSend MessageSystemHandler::Handle(System::MsgDebugSystemMemRecv&& /*data*/,
                                                           std::error_condition& /*errc*/) {
    System::MsgDebugSystemMemSend retData{};
    malloc_trim(0);
    return retData;
}

// Dictionary query
System::MsgDictSend MessageSystemHandler::Handle(System::MsgDictRecv&& data, std::error_condition& /*errc*/) {
    System::MsgDictSend result{};
    for (auto& key : data.keys) {
        auto lowerKey = util::ToLower(key);
        System::MsgDictUnit unit;
        unit.key = key;
        if ("vehiclecolor" == lowerKey) {
            unit.infos = DictVehicleColor();
        } else if ("vehicleplatecolor" == lowerKey) {
            unit.infos = DictVehiclePlateColor();
        } else if ("vehicleclass" == lowerKey) {
            unit.infos = DictVehicleClass();
        } else if ("vehicleorientation" == lowerKey) {
            unit.infos = DictVehicleOrientation();
        } else {
            LOG_INFO("{} Not Support", key);
            continue;
        }
        result.resData.infos.push_back(unit);
    }
    return result;
}

}  // namespace cosmo
