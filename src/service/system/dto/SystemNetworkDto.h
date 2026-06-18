#pragma once

#include <system_error>

#include "service/system/dto/SystemConfigDto.h"
#include "service/system/dto/SystemConfigTypes.h"
#include "util/dto/ServerMsgTypes.h"

namespace cosmo::System {

// HTTP interface parameters set request
struct MsgSetHttpInterfaceParamRecv : public MsgRecvHead {
    bool enable{true};
    std::string url;
};

void to_json(nlohmann::json& j, const MsgSetHttpInterfaceParamRecv& v);
void from_json(const nlohmann::json& j, MsgSetHttpInterfaceParamRecv& v);

// Picture quality set response (Renamed to clear original comment logic)
struct MsgSetHttpInterfaceParamSend : public MsgSendHead {};

// Picture quality query request
struct MsgQueryHttpInterfaceParamRecv : public MsgRecvHead {};

// HTTP interface query response inner data
struct HttpInterfaceParamData {
    bool enable{true};
    std::string url;
    friend void to_json(nlohmann::json& j, const HttpInterfaceParamData& v);
    friend void from_json(const nlohmann::json& j, HttpInterfaceParamData& v);
};

// Picture quality query response
struct MsgQueryHttpInterfaceParamSend : public MsgSendHead {
    HttpInterfaceParamData resData;
};

void to_json(nlohmann::json& j, const MsgQueryHttpInterfaceParamSend& v);
void from_json(const nlohmann::json& j, MsgQueryHttpInterfaceParamSend& v);

// MQTT set request
struct MsgSetMqttAdapterParamRecv : public MsgRecvHead {
    bool enable{true};
    std::string url;
    int port{1883};
    int authMode{0};
    std::string clientId;
    std::string userName;
    std::string passwd;
};

void to_json(nlohmann::json& j, const MsgSetMqttAdapterParamRecv& v);
void from_json(const nlohmann::json& j, MsgSetMqttAdapterParamRecv& v);

// MQTT set request response
struct MsgSetMqttAdapterParamSend : public MsgSendHead {};

// MQTT query request
struct MsgQueryMqttAdapterParamRecv : public MsgRecvHead {};

// MQTT query response inner data
struct MqttAdapterParamData {
    bool enable{true};
    std::string url;
    int port{1883};
    bool status{true};
    int authMode{0};
    std::string clientId;
    std::string userName;
    std::string passwd;
    friend void to_json(nlohmann::json& j, const MqttAdapterParamData& v);
    friend void from_json(const nlohmann::json& j, MqttAdapterParamData& v);
};

// MQTT query response
struct MsgQueryMqttAdapterParamSend : public MsgSendHead {
    MqttAdapterParamData resData;
};

void to_json(nlohmann::json& j, const MsgQueryMqttAdapterParamSend& v);
void from_json(const nlohmann::json& j, MsgQueryMqttAdapterParamSend& v);

// Run mode query request
struct MsgQueryRunModeParamRecv : public MsgRecvHead {};

// Run mode query response inner data
struct RunModeParamData {
    RunMode runMode;
    friend void to_json(nlohmann::json& j, const RunModeParamData& v);
    friend void from_json(const nlohmann::json& j, RunModeParamData& v);
};

// Run mode query response
struct MsgQueryRunModeParamSend : public MsgSendHead {
    RunModeParamData resData;
};

void to_json(nlohmann::json& j, const MsgQueryRunModeParamSend& v);
void from_json(const nlohmann::json& j, MsgQueryRunModeParamSend& v);

// Run mode set request
struct MsgModifyRunModeParamRecv : public MsgRecvHead {
    RunMode runMode;
};

void to_json(nlohmann::json& j, const MsgModifyRunModeParamRecv& v);
void from_json(const nlohmann::json& j, MsgModifyRunModeParamRecv& v);

// Run mode set request response
struct MsgModifyRunModeParamSend : public MsgSendHead {};

struct MsgIotNetworkParam {
    std::string mqttIp;
    int mqttPort{1883};
    std::string httpUrl;
    bool status{true};
    friend void to_json(nlohmann::json& j, const MsgIotNetworkParam& v);
    friend void from_json(const nlohmann::json& j, MsgIotNetworkParam& v);
};

// Network mode parameters query request
struct MsgQueryIotNetworkParamRecv : public MsgRecvHead {};

// Network mode parameters query response
struct MsgQueryIotNetworkParamSend : public MsgSendHead {
    MsgIotNetworkParam resData;
};

void to_json(nlohmann::json& j, const MsgQueryIotNetworkParamSend& v);
void from_json(const nlohmann::json& j, MsgQueryIotNetworkParamSend& v);

// Network mode parameters set request
struct MsgModifyIotNetworkParamRecv : public MsgRecvHead, MsgIotNetworkParam {};

void to_json(nlohmann::json& j, const MsgModifyIotNetworkParamRecv& v);
void from_json(const nlohmann::json& j, MsgModifyIotNetworkParamRecv& v);

// Network mode parameters set request response
struct MsgModifyIotNetworkParamSend : public MsgSendHead {};

}  // namespace cosmo::System
