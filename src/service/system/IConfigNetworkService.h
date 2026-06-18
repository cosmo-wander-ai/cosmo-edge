/// @file IConfigNetworkService.h
/// @brief Network protocol configuration service interface.
///        ISP split from ISystemConfigService.
///        Manages HTTP push, MQTT, and IoT networking parameters.
///        Consumed by NetworkServiceImpl and api/ handlers.
#pragma once

#include <string>

#include "service/system/dto/SystemConfigNetworkDto.h"
#include "util/ErrorCode.h"

namespace cosmo::service {

/// Read/write access to network protocol configuration: HTTP alarm push
/// settings, MQTT broker parameters, and IoT networking endpoints.
class IConfigNetworkService {
public:
    virtual ~IConfigNetworkService() = default;

    // ── HTTP Push ──

    /// Get the current HTTP push configuration.
    /// @return HTTP push parameter structure.
    virtual HttpPushParam GetHttpInterfaceParam() = 0;

    /// Set the HTTP push configuration.
    /// @param param HTTP push parameters to apply.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum SetHttpInterfaceParam(const HttpPushParam& param) = 0;

    // ── MQTT ──

    /// Get the current MQTT broker configuration.
    /// @return MQTT parameter structure.
    virtual MqttParam GetMqttParam() = 0;

    /// Set the MQTT broker configuration.
    /// @param param MQTT parameters to apply.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum SetMqttParam(const MqttParam& param) = 0;

    // ── IoT Networking ──

    /// Get the current IoT networking configuration.
    /// @return IoT network parameter structure.
    virtual IotNetworkParam GetIotNetworkParam() = 0;

    /// Set IoT networking endpoints.
    /// @param httpUrl Platform HTTP API URL.
    /// @param mqttIp  MQTT broker IP address.
    /// @param mqttPort MQTT broker port.
    virtual void SetIotNetworkParam(const std::string& httpUrl, const std::string& mqttIp, int mqttPort) = 0;
};

}  // namespace cosmo::service
