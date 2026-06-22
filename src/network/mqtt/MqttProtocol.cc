// MqttProtocol — MQTT protocol message structures for IoT platform communication.

#include "network/mqtt/MqttProtocol.h"

#include <nlohmann/json.hpp>

#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo::network::mqtt {
void from_json(const nlohmann::json& j, MqttMsgHead& v) {
    if (j.contains("requestId") && !j["requestId"].is_null())
        j.at("requestId").get_to(v.request_id);
    if (j.contains("action") && !j["action"].is_null())
        j.at("action").get_to(v.action);
    if (j.contains("deviceSn") && !j["deviceSn"].is_null())
        j.at("deviceSn").get_to(v.device_sn);
    if (j.contains("msgType") && !j["msgType"].is_null())
        j.at("msgType").get_to(v.msg_type);
}

void to_json(nlohmann::json& j, const MqttMsgHead& v) {
    j["requestId"] = v.request_id;
    j["action"]    = v.action;
    j["deviceSn"]  = v.device_sn;
    j["msgType"]   = v.msg_type;
}

void from_json(const nlohmann::json& j, MqttMsgOnlineSend::Body& v) {
    if (j.contains("devId") && !j["devId"].is_null())
        j.at("devId").get_to(v.dev_id);
    if (j.contains("supplier") && !j["supplier"].is_null())
        j.at("supplier").get_to(v.supplier);
    if (j.contains("aiHostVersion") && !j["aiHostVersion"].is_null())
        j.at("aiHostVersion").get_to(v.ai_host_version);
    if (j.contains("engineType") && !j["engineType"].is_null())
        j.at("engineType").get_to(v.engine_type);
    if (j.contains("deviceModel") && !j["deviceModel"].is_null())
        j.at("deviceModel").get_to(v.device_model);
    if (j.contains("devType") && !j["devType"].is_null())
        j.at("devType").get_to(v.dev_type);
}

void to_json(nlohmann::json& j, const MqttMsgOnlineSend::Body& v) {
    j["devId"]         = v.dev_id;
    j["supplier"]      = v.supplier;
    j["aiHostVersion"] = v.ai_host_version;
    j["engineType"]    = v.engine_type;
    j["deviceModel"]   = v.device_model;
    j["devType"]       = v.dev_type;
}

void from_json(const nlohmann::json& j, MqttMsgHeartBeatSend::Body& v) {
    if (j.contains("devId") && !j["devId"].is_null())
        j.at("devId").get_to(v.dev_id);
    if (j.contains("hostStatus") && !j["hostStatus"].is_null())
        j.at("hostStatus").get_to(v.host_status);
    if (j.contains("customScore") && !j["customScore"].is_null())
        j.at("customScore").get_to(v.custom_score);
    if (j.contains("cpuUsage") && !j["cpuUsage"].is_null())
        j.at("cpuUsage").get_to(v.cpu_usage);
    if (j.contains("memTotal") && !j["memTotal"].is_null())
        j.at("memTotal").get_to(v.mem_total);
    if (j.contains("memAvailable") && !j["memAvailable"].is_null())
        j.at("memAvailable").get_to(v.mem_available);
    if (j.contains("gpuUsage") && !j["gpuUsage"].is_null())
        j.at("gpuUsage").get_to(v.gpu_usage);
    if (j.contains("gpuMemTotal") && !j["gpuMemTotal"].is_null())
        j.at("gpuMemTotal").get_to(v.gpu_mem_total);
    if (j.contains("gpuMemAvailable") && !j["gpuMemAvailable"].is_null())
        j.at("gpuMemAvailable").get_to(v.gpu_mem_available);
    if (j.contains("diskTotal") && !j["diskTotal"].is_null())
        j.at("diskTotal").get_to(v.disk_total);
    if (j.contains("diskAvailable") && !j["diskAvailable"].is_null())
        j.at("diskAvailable").get_to(v.disk_available);
}

void to_json(nlohmann::json& j, const MqttMsgHeartBeatSend::Body& v) {
    j["devId"]           = v.dev_id;
    j["hostStatus"]      = v.host_status;
    j["customScore"]     = v.custom_score;
    j["cpuUsage"]        = v.cpu_usage;
    j["memTotal"]        = v.mem_total;
    j["memAvailable"]    = v.mem_available;
    j["gpuUsage"]        = v.gpu_usage;
    j["gpuMemTotal"]     = v.gpu_mem_total;
    j["gpuMemAvailable"] = v.gpu_mem_available;
    j["diskTotal"]       = v.disk_total;
    j["diskAvailable"]   = v.disk_available;
}

void from_json(const nlohmann::json& j, MqttCommonMsgDl& v) {
    if (j.contains("head") && !j["head"].is_null())
        j.at("head").get_to(v.head);
    if (j.contains("body") && !j["body"].is_null())
        j.at("body").get_to(v.body);
}

void to_json(nlohmann::json& j, const MqttCommonMsgDl& v) {
    j["head"] = v.head;
    j["body"] = v.body;
}

void from_json(const nlohmann::json& j, MqttCommonMsgUl& v) {
    if (j.contains("head") && !j["head"].is_null())
        j.at("head").get_to(v.head);
    if (j.contains("body") && !j["body"].is_null())
        j.at("body").get_to(v.body);
}

void to_json(nlohmann::json& j, const MqttCommonMsgUl& v) {
    j["head"] = v.head;
    j["body"] = v.body;
}

void to_json(nlohmann::json& j, const MqttMsgOnlineSend& v) {
    j["head"] = v.head;
    j["body"] = v.body;
}

void from_json(const nlohmann::json& j, MqttMsgOnlineSend& v) {
    if (j.contains("head") && !j["head"].is_null())
        j.at("head").get_to(v.head);
    if (j.contains("body") && !j["body"].is_null())
        j.at("body").get_to(v.body);
}

void to_json(nlohmann::json& j, const MqttMsgHeartBeatSend& v) {
    j["head"] = v.head;
    j["body"] = v.body;
}

void from_json(const nlohmann::json& j, MqttMsgHeartBeatSend& v) {
    if (j.contains("head") && !j["head"].is_null())
        j.at("head").get_to(v.head);
    if (j.contains("body") && !j["body"].is_null())
        j.at("body").get_to(v.body);
}

}  // namespace cosmo::network::mqtt
