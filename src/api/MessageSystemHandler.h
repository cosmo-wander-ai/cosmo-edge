// MessageSystemHandler.h — System message handler.
//
// Implementation partitions (methods declared here, defined in separate .cc):
//   MessageSystemHandlerOps.cc  — handler operations (logo, debug, config, etc.)

#pragma once

#include <system_error>

#include "service/system/dto/SystemDebugDto.h"
#include "service/system/dto/SystemDeviceDto.h"
#include "service/system/dto/SystemMaintainDto.h"
#include "service/system/dto/SystemNetworkDto.h"
#include "service/system/dto/SystemParamDto.h"
#include "service/system/dto/SystemTimeDto.h"

namespace cosmo::service {
class IConfigReadService;
class IConfigWriteService;
class IConfigNetworkService;
class IDeviceInfoService;
class ISystemOperationService;
class ITimeService;
}  // namespace cosmo::service

namespace cosmo {

// System message handler — delegates to injected service interfaces.
class MessageSystemHandler {
public:
    MessageSystemHandler(service::IConfigReadService& config_read, service::IConfigWriteService& config_write,
                         service::IConfigNetworkService& config_network,
                         service::IDeviceInfoService& device_info,
                         service::ISystemOperationService& system_op, service::ITimeService& time_service);

    System::MsgQueryDeviceInfoSend Handle(System::MsgQueryDeviceInfoRecv&& data,
                                          std::error_condition& errc);  //
    System::MsgQueryHardwareResourceSend Handle(System::MsgQueryHardwareResourceRecv&& data,
                                                std::error_condition& errc);  //

    System::MsgQueryTimeSend Handle(System::MsgQueryTimeRecv&& data, std::error_condition& errc);    //
    System::MsgNTPDateSend Handle(System::MsgNTPDateRecv&& data, std::error_condition& errc);        //
    System::MsgModifyTimeSend Handle(System::MsgModifyTimeRecv&& data, std::error_condition& errc);  //

    System::MsgSetPictureQualitySend Handle(System::MsgSetPictureQualityRecv&& data,
                                            std::error_condition& errc);  //
    System::MsgQueryPictureQualitySend Handle(System::MsgQueryPictureQualityRecv&& data,
                                              std::error_condition& errc);  //
    System::MsgResetPictureQualitySend Handle(System::MsgResetPictureQualityRecv&& data,
                                              std::error_condition& errc);  //

    System::MsgSetAlarmVideoDurationSend Handle(System::MsgSetAlarmVideoDurationRecv&& data,
                                                std::error_condition& errc);  //
    System::MsgQueryAlarmVideoDurationSend Handle(System::MsgQueryAlarmVideoDurationRecv&& data,
                                                  std::error_condition& errc);  //
    System::MsgResetAlarmVideoDurationSend Handle(System::MsgResetAlarmVideoDurationRecv&& data,
                                                  std::error_condition& errc);  //

    System::MsgModifyDevRestartParamSend Handle(System::MsgModifyDevRestartParamRecv&& data,
                                                std::error_condition& errc);  //
    System::MsgQueryDevRestartParamSend Handle(System::MsgQueryDevRestartParamRecv&& data,
                                               std::error_condition& errc);  //
    System::MsgResetDevRestartParamSend Handle(System::MsgResetDevRestartParamRecv&& data,
                                               std::error_condition& errc);  //

    System::MsgResetSystemSend Handle(System::MsgResetSystemRecv&& data, std::error_condition& errc);  //

    System::MsgExportFileSend Handle(System::MsgExportFileRecv&& data, std::error_condition& errc);  //

    System::MsgUpgradeSend Handle(System::MsgUpgradeRecv&& data, std::error_condition& errc);  //

    System::MsgQuerySystemLogoSend Handle(System::MsgQuerySystemLogoRecv&& data,
                                          std::error_condition& errc);                                     //
    System::MsgSetSystemLogoSend Handle(System::MsgSetSystemLogoRecv&& data, std::error_condition& errc);  //

    System::MsgQueryDeviceStatusSend Handle(System::MsgQueryDeviceStatusRecv&& data,
                                            std::error_condition& errc);  //

    System::MsgModifyDebugModeSend Handle(System::MsgModifyDebugModeRecv&& data,
                                          std::error_condition& errc);  //
    System::MsgQueryDebugModeSend Handle(System::MsgQueryDebugModeRecv&& data,
                                         std::error_condition& errc);  //

    System::MsgModifyShiledActionsSend Handle(System::MsgModifyShiledActionsRecv&& data,
                                              std::error_condition& errc);  //
    System::MsgQueryShiledActionsSend Handle(System::MsgQueryShiledActionsRecv&& data,
                                             std::error_condition& errc);  //

    System::MsgThreadDebugInfoSend Handle(System::MsgThreadDebugInfoRecv&& data,
                                          std::error_condition& errc);  //

    System::MsgQueryPopUpParamSend Handle(System::MsgQueryPopUpParamRecv&& data,
                                          std::error_condition& errc);                                     //
    System::MsgSetPopUpParamSend Handle(System::MsgSetPopUpParamRecv&& data, std::error_condition& errc);  //

    System::MsgQueryHttpInterfaceParamSend Handle(System::MsgQueryHttpInterfaceParamRecv&& data,
                                                  std::error_condition& errc);  //
    System::MsgSetHttpInterfaceParamSend Handle(System::MsgSetHttpInterfaceParamRecv&& data,
                                                std::error_condition& errc);  //

    System::MsgQueryMqttAdapterParamSend Handle(System::MsgQueryMqttAdapterParamRecv&& data,
                                                std::error_condition& errc);  //
    System::MsgSetMqttAdapterParamSend Handle(System::MsgSetMqttAdapterParamRecv&& data,
                                              std::error_condition& errc);  //

    System::MsgQueryRunModeParamSend Handle(System::MsgQueryRunModeParamRecv&& data,
                                            std::error_condition& errc);  //
    System::MsgModifyRunModeParamSend Handle(System::MsgModifyRunModeParamRecv&& data,
                                             std::error_condition& errc);  //

    System::MsgQueryIotNetworkParamSend Handle(System::MsgQueryIotNetworkParamRecv&& data,
                                               std::error_condition& errc);  //
    System::MsgModifyIotNetworkParamSend Handle(System::MsgModifyIotNetworkParamRecv&& data,
                                                std::error_condition& errc);  //

    System::MsgQueryDocumentUrlSend Handle(System::MsgQueryDocumentUrlRecv&& data,
                                           std::error_condition& errc);  //

    System::MsgQueryResourceLimitParamSend Handle(System::MsgQueryResourceLimitParamRecv&& data,
                                                  std::error_condition& errc);  //
    System::MsgSetResourceLimitParamSend Handle(System::MsgSetResourceLimitParamRecv&& data,
                                                std::error_condition& errc);  //

    System::MsgDebugQuitSend Handle(System::MsgDebugQuitRecv&& data, std::error_condition& errc);  //
    System::MsgDebugSystemMemSend Handle(System::MsgDebugSystemMemRecv&& data,
                                         std::error_condition& errc);  //

    System::MsgDictSend Handle(System::MsgDictRecv&& data, std::error_condition& errc);  //

private:
    service::IConfigReadService& config_read_;
    service::IConfigWriteService& config_write_;
    service::IConfigNetworkService& config_network_;
    service::IDeviceInfoService& device_info_;
    service::ISystemOperationService& system_op_;
    service::ITimeService& time_service_;
};

}  // namespace cosmo
