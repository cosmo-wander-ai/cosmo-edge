#pragma once

// MQTT protocol message structures for IoT platform communication.
// Extracted from MqttAdapter.h during CMqttAdapter → MqttLifecycleServiceImpl merge.

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace cosmo::network::mqtt {

struct MqttMsgHead {
    std::string request_id;
    std::string action{""};
    std::string device_sn;
    std::string msg_type;
    friend void to_json(nlohmann::json& j, const MqttMsgHead& v);
    friend void from_json(const nlohmann::json& j, MqttMsgHead& v);
};

// Registration/online request message
struct MqttMsgOnlineSend {
    MqttMsgHead head;
    struct Body {
        std::string dev_id;
        std::string supplier{"CWAI"};
        std::string ai_host_version;
        std::string engine_type;
        std::string device_model;
        int dev_type{0};  // 0: AAE; 1: third-party; 2: AAE box
        friend void to_json(nlohmann::json& j, const Body& v);
        friend void from_json(const nlohmann::json& j, Body& v);
    } body;
    friend void to_json(nlohmann::json& j, const MqttMsgOnlineSend& v);
    friend void from_json(const nlohmann::json& j, MqttMsgOnlineSend& v);
};

struct MqttMsgHeartBeatSend {
    MqttMsgHead head;
    struct Body {
        std::string dev_id;
        int host_status{0};
        double custom_score{1};
        float cpu_usage{0};
        size_t mem_total{0};
        size_t mem_available{0};
        float gpu_usage{0};
        size_t gpu_mem_total{0};
        size_t gpu_mem_available{0};
        size_t disk_total{0};
        size_t disk_available{0};
        friend void to_json(nlohmann::json& j, const Body& v);
        friend void from_json(const nlohmann::json& j, Body& v);
    } body;
    friend void to_json(nlohmann::json& j, const MqttMsgHeartBeatSend& v);
    friend void from_json(const nlohmann::json& j, MqttMsgHeartBeatSend& v);
};

// Downstream transparent message
struct MqttCommonMsgDl {
    MqttMsgHead head;
    std::string body;
    friend void to_json(nlohmann::json& j, const MqttCommonMsgDl& v);
    friend void from_json(const nlohmann::json& j, MqttCommonMsgDl& v);
};

// Upstream transparent message
struct MqttCommonMsgUl {
    MqttMsgHead head;
    std::string body;
    friend void to_json(nlohmann::json& j, const MqttCommonMsgUl& v);
    friend void from_json(const nlohmann::json& j, MqttCommonMsgUl& v);
};

}  // namespace cosmo::network::mqtt
