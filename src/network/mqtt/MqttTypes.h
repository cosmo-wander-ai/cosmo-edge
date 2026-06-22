#pragma once

// MQTT client common types and definitions.
// Migrated from cwai::AIoT_MQTT to cosmo::network::mqtt.

#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace cosmo::network::mqtt {

// Error codes
enum class MqttErrorCode {
    kSuccess = 0,
    // 0x0400 = 1024
    kServerUriEmpty = 0x0400,
    kClientIdEmpty,
    kUsernameEmpty,
    kPasswordEmpty,
    kAuthTypeInvalid,
    kAuthKeyEmpty,
    kAuthDeviceTypeEmpty,
    kAuthDeviceSnEmpty,
    kClientNotCreated,
    kContextIsNull,
    kWaitAckTimeout,
    kWaitResponseTimeout,
    kOffline
};

// Async-to-sync publish states (bitmask)
enum class SyncPubState : int { kError = 0x00, kSent = 0x01, kAcked = 0x02, kResponded = 0x04 };

// Log level
enum class MqttLogLevel {
    kOff = 0,
    kFatal,
    kError,
    kWarn,
    kInfo,
    kDebug,
    kTrace,
    kAll,
};

// Authentication type
enum class MqttAuthType {
    // cwai IoT: password derived from salt + key + device_type + SN
    kMvIot = 1,
    // Standard username/password authentication
    kNormal = 2,
    // Anonymous, no authentication
    kAnonymous = 3
};

// Connection configuration options
struct MqttConnectOptions {
    // Create parameters
    std::string server_uri;  // Format: protocol://host:port or host:port
    std::string client_id;   // UTF-8 encoded
    // Connect options
    int keep_alive_interval_s = 60;    // Heartbeat period in seconds
    int reconnect_interval_ms = 5000;  // Reconnect interval in milliseconds
    int connect_timeout_s     = 5;     // Connection timeout in seconds
    // Message options
    int max_inflight_msgs = -1;  // Max in-flight QoS 1/2 messages, -1 = default (10)

    // Authentication
    MqttAuthType auth_type = MqttAuthType::kMvIot;
    std::string username;
    std::string password;
    std::string auth_key;
    std::string device_type;
    std::string device_sn;
};

// MQTT connection config + subscription info
struct MqttClientInfos {
    MqttConnectOptions connect_opts;
    std::map<std::string, int> subscriptions;
};

// MQTT message
struct MqttMessage {
    std::string topic;
    std::string payload;
    int qos = 0;
};

// Async-to-sync publish result
struct SyncPubResult : public MqttMessage {
    std::string request_id;
    int sync_state = static_cast<int>(SyncPubState::kError);
};

// Incoming subscription message
struct MessageArrived : public MqttMessage {
    // Extensible; no extra fields needed yet
};

// Callback types
using MessageArrivedCallback   = std::function<void(MessageArrived&&, void*)>;
using MqttLogCallback          = std::function<void(int level, const std::string& msg)>;
using ConnectionLostCallback   = std::function<void(void*)>;
using ReconnectSuccessCallback = std::function<void(void*)>;

}  // namespace cosmo::network::mqtt
