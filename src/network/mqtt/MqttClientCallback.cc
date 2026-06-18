// Paho MQTT C library callback implementations.
// Uses CMvMQTTClient public methods (no friend functions).

#include "network/mqtt/MqttClientInternal.h"
#include "nlohmann/json.hpp"

using nlohmann::json;
namespace mqtt = cosmo::network::mqtt;

// Connection lost callback — notifies user and triggers reconnection path.
void cosmo::network::mqtt::MQTTClient_ConnectionLost_CB(void* context, char* cause) {
    auto* client = static_cast<mqtt::CMvMQTTClient*>(context);
    if (nullptr == client) {
        LOG_ERRO("{}", "[connLost] Fatal error: context is NULL and never try to reconnect");
        return;
    }
    LOG_ERRO("[connLost] Callback: connection lost, cause: [{}]", cause ? cause : "null");

    // Notify user that connection has been lost
    client->InvokeConnectionLostCallback();
    // External reconnection — callback caller handles reconnect
    client->InvokeReconnectCallback();
}

// Subscription message arrival callback — handles async-to-sync correlation.
int cosmo::network::mqtt::MQTTClient_MessageArrived_CB(void* context, char* topicName, int /*topicLen*/,
                                                       MQTTClient_message* m) {
    LOG_INFO(
        "[msg] Callback: message received on topic [{}], msgid: [{}], "
        "dup: [{}], retained: [{}], qos: [{}], payloadlen: [{}]",
        topicName, m->msgid, m->dup, m->retained, m->qos, m->payloadlen);

    auto* client = static_cast<mqtt::CMvMQTTClient*>(context);
    if (nullptr == client) {
        MQTTClient_free(topicName);
        MQTTClient_freeMessage(&m);
        LOG_ERRO("{}", "[msg] Fatal error: context is NULL and all messages will be lost");
        return static_cast<int>(mqtt::MqttErrorCode::kContextIsNull);
    }

    std::string request_id;
    std::string topic   = topicName;
    std::string payload = std::string(static_cast<char*>(m->payload), m->payloadlen);
    int qos             = m->qos;
    MQTTClient_free(topicName);
    MQTTClient_freeMessage(&m);

    bool is_sync_msg = false;

    // Try to parse the requestId from the JSON payload for async-to-sync correlation
    try {
        auto j     = json::parse(payload);
        request_id = j.at("head").at("requestId").get<std::string>();
    } catch (const nlohmann::detail::exception& e) {
        LOG_WARN("[msg] Parsing requestId of head failed, catch: {}", e.what());
        LOG_WARN("[msg] Payload: {}", payload);
    }

    if (!request_id.empty()) {
        const auto& prefix = client->GetSyncRequestIdPrefix();
        // Empty prefix means all subscribed messages are checked for sync correlation
        if (prefix.empty() || request_id.compare(0, prefix.length(), prefix) == 0) {
            mqtt::SyncPubResult sync_result;
            sync_result.sync_state = static_cast<int>(mqtt::SyncPubState::kAcked);
            sync_result.qos        = qos;
            sync_result.request_id = request_id;
            sync_result.payload    = payload;
            sync_result.topic      = topic;

            is_sync_msg = client->UpdateSyncResult(sync_result);
            if (!is_sync_msg) {
                LOG_INFO("[msg] Message with requestId: [{}] don't need to or can't notify sync wait",
                         request_id);
            }
        }
    }

    // If not a sync-correlated response, invoke the user's message callback
    if (!is_sync_msg) {
        mqtt::MessageArrived arrived;
        arrived.qos     = qos;
        arrived.payload = std::move(payload);
        arrived.topic   = std::move(topic);
        client->InvokeMessageCallback(arrived);
    }
    return mqtt::kMsgReceived;
}

// Delivery complete callback — called for QoS > 0 messages.
void cosmo::network::mqtt::MQTTClient_DeliveryComplete_CB(void* /*context*/, MQTTClient_deliveryToken dt) {
    static std::atomic<uint64_t> delivery_count{0};
    ++delivery_count;
    LOG_INFO("[delivery] Delivery complete msg count: [{}], delivery token: [{}]", delivery_count.load(), dt);
}
