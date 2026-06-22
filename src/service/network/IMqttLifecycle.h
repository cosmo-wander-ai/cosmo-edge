/// @file IMqttLifecycle.h
/// @brief MQTT lifecycle interface — start/stop control for the MQTT client.
///        ISP split from INetworkService.
///        Consumed by app_init.cc for MQTT start/stop control.
#pragma once

namespace cosmo::service {

/// Controls the MQTT client lifecycle: registration check, enable/disable
/// state, and start/stop operations.
class IMqttLifecycle {
public:
    virtual ~IMqttLifecycle() = default;

    /// Check whether MQTT is registered (configured in the system).
    /// @return true if MQTT configuration exists.
    virtual bool IsMqttRegistered() = 0;

    /// Check whether MQTT is currently enabled.
    /// @return true if MQTT is enabled.
    virtual bool IsMqttEnabled() = 0;

    /// Stop the MQTT client and disconnect from the broker.
    virtual void MqttStop() = 0;

    /// Start the MQTT client and connect to the configured broker.
    virtual void MqttStart() = 0;
};

}  // namespace cosmo::service
