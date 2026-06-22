// Data structures for system configuration (extracted from CfgSystemParam.h).
// These are pure data types with JSON serialization — no singleton, no macros.

#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

#include "util/ErrorCode.h"

namespace cosmo {
enum class RunMode {
    RunModeStandAlone = 0,  // Standalone version
    RunModeIotNetwork = 1   // Network version
};
void to_json(nlohmann::json& j, const RunMode& v);
void from_json(const nlohmann::json& j, RunMode& v);

struct CfgSystemDebugInfo {
    bool bDebugOpen{false};
    std::vector<std::string> shieldActions;
    friend void to_json(nlohmann::json& j, const CfgSystemDebugInfo& v);
    friend void from_json(const nlohmann::json& j, CfgSystemDebugInfo& v);
};

struct CfgSystemLogoInfo {
    std::string bigScreenName;
    bool bHaveSetLogo{false};
    std::string systemName;
    std::string logoFileName;
    CfgSystemDebugInfo debugInfo;
    friend void to_json(nlohmann::json& j, const CfgSystemLogoInfo& v);
    friend void from_json(const nlohmann::json& j, CfgSystemLogoInfo& v);
};

struct CfgSystemPopUpInfo {
    int popUpSwitch{1};
    int audioPlay{1};
    int popUpDuration{2};
    friend void to_json(nlohmann::json& j, const CfgSystemPopUpInfo& v);
    friend void from_json(const nlohmann::json& j, CfgSystemPopUpInfo& v);
};

struct CfgSystemResourceInfo {
    bool bResourceLimitOpen{true};
    friend void to_json(nlohmann::json& j, const CfgSystemResourceInfo& v);
    friend void from_json(const nlohmann::json& j, CfgSystemResourceInfo& v);
};

struct MqttUnitParam {
    bool bEnable{false};
    std::string ip;
    int port{1883};
    int authMode{0};
    std::string clientId;
    std::string userName;
    std::string passwd;
    friend void to_json(nlohmann::json& j, const MqttUnitParam& v);
    friend void from_json(const nlohmann::json& j, MqttUnitParam& v);
};

struct MqttHttpParam {
    bool bEnable{false};
    std::string url;
    std::string ip;
    int port{80};
    friend void to_json(nlohmann::json& j, const MqttHttpParam& v);
    friend void from_json(const nlohmann::json& j, MqttHttpParam& v);
};

struct CfgSystemInterfaceModeIotInfo {
    MqttUnitParam mqttParam;
    MqttHttpParam httpParam;
    friend void to_json(nlohmann::json& j, const CfgSystemInterfaceModeIotInfo& v);
    friend void from_json(const nlohmann::json& j, CfgSystemInterfaceModeIotInfo& v);
};

struct CfgSystemInterfaceModeInfo {
    RunMode runMode{RunMode::RunModeStandAlone};
    CfgSystemInterfaceModeIotInfo iotParam;
    CfgSystemInterfaceModeIotInfo standAloneParam;
    friend void to_json(nlohmann::json& j, const CfgSystemInterfaceModeInfo& v);
    friend void from_json(const nlohmann::json& j, CfgSystemInterfaceModeInfo& v);
};

struct CfgSystemParamInfo {
    CfgSystemLogoInfo logoInfo;
    CfgSystemPopUpInfo popUpInfo;
    CfgSystemResourceInfo resourceInfo;
    CfgSystemInterfaceModeInfo interfaceModeInfo;
    friend void to_json(nlohmann::json& j, const CfgSystemParamInfo& v);
    friend void from_json(const nlohmann::json& j, CfgSystemParamInfo& v);
};

struct CfgSystemTmpParam {
    CfgSystemDebugInfo debugInfo;
    friend void to_json(nlohmann::json& j, const CfgSystemTmpParam& v);
    friend void from_json(const nlohmann::json& j, CfgSystemTmpParam& v);
};

}  // namespace cosmo
