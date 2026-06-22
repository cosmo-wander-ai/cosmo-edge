// Network-related configuration DTO types — HTTP push, MQTT, IoT parameters.
// Extracted from ISystemConfigService.h as part of DEBT-A01 ISP split.
#pragma once

#include <string>

namespace cosmo::service {

// HTTP push parameters
struct HttpPushParam {
    bool enable{false};
    std::string url;
};

// MQTT parameters
struct MqttParam {
    bool enable{true};
    std::string url;
    int port{1883};
    int authMode{0};
    std::string clientId;
    std::string userName;
    std::string passwd;
    bool status{true};
};

// IoT network parameters
struct IotNetworkParam {
    std::string mqttIp;
    int mqttPort{1883};
    std::string httpUrl;
    bool status{true};
};

// Logo info
struct SystemLogoInfo {
    std::string systemName;
    std::string logoUrl;
    std::string bigScreenName;
};

}  // namespace cosmo::service
