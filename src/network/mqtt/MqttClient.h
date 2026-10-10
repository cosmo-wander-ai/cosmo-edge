#pragma once

// CMvMQTTClient — MQTT client wrapping the Paho C library.
// Does not support MQTT 5.
//
// Async-to-sync: requires JSON payload with head.requestId (unique).
// Migrated from cwai::AIoT_MQTT to cosmo::network::mqtt.

#include <condition_variable>
#include <map>
#include <mutex>
#include <string>

#include "network/mqtt/MqttTypes.h"

namespace cosmo::network::mqtt {

class CMvMQTTClient {
public:
    // @param sync_prefix  Prefix for requestId to identify sync-correlated messages.
    //                     If empty, all messages are checked for sync correlation.
    explicit CMvMQTTClient(const std::string& sync_prefix = "");
    ~CMvMQTTClient();

    // Non-copyable
    CMvMQTTClient(const CMvMQTTClient&)            = delete;
    CMvMQTTClient& operator=(const CMvMQTTClient&) = delete;

    // Create MQTT client: allocate resources and connect to the server.
    // Returns 0 on success, negative on failure.
    int MQTTClientCreate(const MqttConnectOptions& opts);

    // Set callbacks for message arrival, connection lost, and reconnect success.
    // Returns 0 on success.
    int MQTTClientSetCallbacks(void* user_context, MessageArrivedCallback on_message,
                               ConnectionLostCallback on_connect_lost, ReconnectSuccessCallback on_reconnect);

    // Subscribe to a topic with given QoS. Returns 0 on success.
    int MQTTClientSubscribe(const std::string& topic, int qos = 0);

    // Unsubscribe from a topic. Returns 0 on success.
    int MQTTClientUnSubscribe(const std::string& topic);

    // Publish asynchronously, or wait for one business ACK with a matching requestId.
    // @param msg             Message to publish
    // @param ack_timeout_ms  Timeout for ACK wait (<=0 to send asynchronously)
    // @param sync_result     Output for ACK result (nullptr to send asynchronously)
    // Returns 0 on success.
    int MQTTClientPublish(const MqttMessage& msg, int ack_timeout_ms = 0,
                          SyncPubResult* sync_result = nullptr);

    // Disconnect and release all resources.
    // @param timeout_ms  Disconnect timeout (0 = discard pending messages)
    int MQTTClientDestroy(int timeout_ms = 0);

    // Check if connected to the MQTT server.
    bool MQTTClientIsConnected();

    // --- Methods used by Paho C callbacks (public to avoid friend functions) ---

    // Invoke the message arrived callback.
    void InvokeMessageCallback(MessageArrived& msg);

    // Invoke the connection lost callback.
    void InvokeConnectionLostCallback();

    // Invoke the reconnect success callback.
    void InvokeReconnectCallback();

    // Update sync publish result from an incoming response.
    // Returns true if a matching pending request was found.
    bool UpdateSyncResult(const SyncPubResult& result);

    // Get the sync request ID prefix.
    const std::string& GetSyncRequestIdPrefix() const;

private:
    // Check connection status under existing lock.
    bool IsConnectedLocked() const;

    // --- Publish helpers (extracted from monolithic MQTTClientPublish) ---
    // Parse requestId from JSON payload and pre-insert sync cache entry.
    // Returns the requestId (empty if not applicable).
    std::string PrepareSyncPublish(const MqttMessage& msg);

    // Wait for a business ACK after successful publish.
    int WaitForAck(const std::string& request_id, int ack_timeout_ms, SyncPubResult* sync_result);

    // Paho MQTT client handle
    void* mqtt_handle_{nullptr};

    // Sync requestId prefix
    std::string sync_prefix_;

    // User context for callbacks
    void* user_context_{nullptr};

    // Callbacks
    MessageArrivedCallback on_message_;
    ConnectionLostCallback on_connect_lost_;
    ReconnectSuccessCallback on_reconnect_;

    // Protects mqtt_handle_, callbacks, user_context_
    std::mutex mutex_;

    // Own the strings referenced by Paho connection options.
    MqttConnectOptions connect_opts_;

    // Async-to-sync publish: pending results keyed by requestId
    std::map<std::string, SyncPubResult> sync_results_;
    std::mutex sync_mutex_;
    std::condition_variable sync_cv_;
};

}  // namespace cosmo::network::mqtt
