// CMvMQTTClient class method implementations.
// Paho C callbacks   → MvMQTTClientCallback.cc
// Utility functions  → MvMQTTClientUtil.cc
// Shared declarations → MqttClientInternal.h

#include <vector>

#include "network/mqtt/MqttClientInternal.h"
#include "nlohmann/json.hpp"

using nlohmann::json;

namespace cosmo::network::mqtt {

CMvMQTTClient::CMvMQTTClient(const std::string& sync_prefix) : sync_prefix_(sync_prefix) {}

CMvMQTTClient::~CMvMQTTClient() {
    enable_reconnect_ = false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (MQTTClient_isConnected(mqtt_handle_)) {
        LOG_INFO("{}", "[create] MQTTClient is still connected in destructor");
        MQTTClient_disconnect(mqtt_handle_, 0);
    }
    MQTTClient_destroy(&mqtt_handle_);
    user_context_    = nullptr;
    on_message_      = nullptr;
    on_reconnect_    = nullptr;
    on_connect_lost_ = nullptr;
}

int CMvMQTTClient::MQTTClientCreate(const MqttConnectOptions& opts) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Disconnect if still connected
    if (MQTTClient_isConnected(mqtt_handle_)) {
        LOG_WARN("{}", "[create] MQTTClient is still connected when ready to create");
        MQTTClient_disconnect(mqtt_handle_, 0);
    }

    // Destroy existing handle if present
    if (mqtt_handle_) {
        LOG_WARN("[create] MQTTClient handle is still not nullptr: [{}]", mqtt_handle_);
        MQTTClient_destroy(&mqtt_handle_);
    }

    // Validate parameters
    auto ret = ValidateConnectParams(opts);
    if (static_cast<int>(MqttErrorCode::kSuccess) != ret) {
        LOG_ERRO("[create] MQTTClient Validate Connect Params failed, error code: [{}]", ret);
        return ret;
    }

    // Save connect options for reconnection
    connect_opts_ = opts;

    do {
        ret = MQTTClient_create(&mqtt_handle_, opts.server_uri.c_str(), opts.client_id.c_str(),
                                MQTTCLIENT_PERSISTENCE_NONE, nullptr);
        if (MQTTCLIENT_SUCCESS != ret) {
            LOG_ERRO("[create] MQTTClient_create failed, return code: [{}]", ret);
            break;
        }
        LOG_INFO("[create] MQTTClient handle create success, handle: [{}], this: [{}]", mqtt_handle_,
                 static_cast<const void*>(this));

        ret = MQTTClient_setCallbacks(mqtt_handle_, this, MQTTClient_ConnectionLost_CB,
                                      MQTTClient_MessageArrived_CB, MQTTClient_DeliveryComplete_CB);
        if (MQTTCLIENT_SUCCESS != ret) {
            LOG_ERRO("[create] MQTTClient_setCallbacks failed, return code: [{}]", ret);
            break;
        }
        LOG_INFO("{}", "[create] MQTTClient setCallbacks success");

        // Initialize connection options
        MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
        InitConnectOptions(connect_opts_, &conn_opts);

        // reliable=0 makes maxInflightMessages default to 10 instead of 1
        conn_opts.reliable = 0;

        ret = MQTTClient_connect(mqtt_handle_, &conn_opts);
        if (MQTTCLIENT_SUCCESS != ret) {
            LOG_ERRO("[create] MQTTClient_connect failed, Error info: [{}, {}]", ret,
                     MQTTClient_strerror(ret));
            break;
        }
    } while (false);

    if (ret) {
        MQTTClient_destroy(&mqtt_handle_);
    } else {
        enable_reconnect_ = true;
        LOG_INFO("{}", "[create] MQTTClient_connect success");
    }
    return ret;
}

int CMvMQTTClient::MQTTClientSetCallbacks(void* user_context, MessageArrivedCallback on_message,
                                          ConnectionLostCallback on_connect_lost,
                                          ReconnectSuccessCallback on_reconnect) {
    std::lock_guard<std::mutex> lock(mutex_);
    user_context_    = user_context;
    on_message_      = std::move(on_message);
    on_connect_lost_ = std::move(on_connect_lost);
    on_reconnect_    = std::move(on_reconnect);
    return static_cast<int>(MqttErrorCode::kSuccess);
}

int CMvMQTTClient::MQTTClientSubscribe(const std::string& topic, int qos) {
    LOG_INFO("[sub] MQTTClient ready to subscribe, topic: [{}], qos: [{}]", topic, qos);

    int ret = MQTTCLIENT_SUCCESS;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (nullptr == mqtt_handle_) {
            LOG_ERRO("{}", "[sub] MQTTClient handle is nullptr");
            return static_cast<int>(MqttErrorCode::kClientNotCreated);
        }
        ret = MQTTClient_subscribe(mqtt_handle_, topic.c_str(), qos);
    }

    if (MQTTCLIENT_SUCCESS != ret) {
        LOG_ERRO("[sub] MQTTClient_subscribe failed, topic: [{}], qos: [{}], Error: [{}, {}]", topic, qos,
                 ret, MQTTClient_strerror(ret));
        return ret;
    }
    LOG_INFO("[sub] MQTTClient subscribe success, topic: [{}], qos: [{}]", topic, qos);

    InsertSubInfo(topic, qos);
    return ret;
}

int CMvMQTTClient::MQTTClientUnSubscribe(const std::string& topic) {
    LOG_INFO("[unsub] MQTTClient ready to unsubscribe, topic: [{}]", topic);

    int ret = MQTTCLIENT_SUCCESS;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (nullptr == mqtt_handle_) {
            LOG_ERRO("{}", "[unsub] MQTTClient handle is nullptr");
            return static_cast<int>(MqttErrorCode::kClientNotCreated);
        }
        ret = MQTTClient_unsubscribe(mqtt_handle_, topic.c_str());
    }

    if (MQTTCLIENT_SUCCESS != ret) {
        LOG_ERRO("[unsub] MQTTClient_unsubscribe failed, topic: [{}], Error: [{}, {}]", topic, ret,
                 MQTTClient_strerror(ret));
        return ret;
    }
    LOG_INFO("[unsub] MQTTClient unsubscribe success, topic: [{}]", topic);

    EraseSubInfo(topic);
    return ret;
}

std::string CMvMQTTClient::PrepareSyncPublish(const MqttMessage& msg, SyncPubResult* const sync_result) {
    std::string request_id;
    if (nullptr == sync_result) {
        return request_id;
    }

    try {
        auto j     = json::parse(msg.payload);
        request_id = j.at("head").at("requestId").get<std::string>();
    } catch (const nlohmann::detail::exception& e) {
        LOG_WARN("[pub] parsing JSON failed, catch: {}", e.what());
    }

    if (!request_id.empty()) {
        SyncPubResult entry;
        entry.sync_state = static_cast<int>(SyncPubState::kSent);
        entry.request_id = request_id;

        std::unique_lock<std::mutex> lock(sync_mutex_);
        auto [it, inserted] = sync_results_.emplace(request_id, std::move(entry));
        if (!inserted) {
            LOG_ERRO("[pub] Already existing requestId: [{}]", request_id);
            request_id.clear();
        }
    }
    return request_id;
}

int CMvMQTTClient::WaitForSyncResponse(const std::string& request_id, int ack_timeout_ms,
                                       int response_timeout_ms, SyncPubResult* sync_result) {
    int ret = MQTTCLIENT_SUCCESS;
    std::unique_lock<std::mutex> lock(sync_mutex_);

    auto check_state = [&](int required_state) {
        auto it = sync_results_.find(request_id);
        if (it != sync_results_.end()) {
            *sync_result = it->second;
            return (it->second.sync_state & required_state) != 0;
        }
        return false;
    };

    // Wait for ACK if requested
    if (ack_timeout_ms > 0) {
        LOG_INFO("[pub] Wait ACK for requestId: [{}]", request_id);
        if (sync_cv_.wait_for(lock, std::chrono::milliseconds(ack_timeout_ms), [&] {
                return check_state(static_cast<int>(SyncPubState::kAcked) |
                                   static_cast<int>(SyncPubState::kResponded));
            })) {
            LOG_INFO("[pub] Wait ACK success, requestId: [{}], state: [{}]", request_id,
                     sync_result->sync_state);

            // Wait for response if requested
            if (response_timeout_ms > 0) {
                LOG_INFO("[pub] Wait response for requestId: [{}]", request_id);
                if (!sync_cv_.wait_for(lock, std::chrono::milliseconds(response_timeout_ms), [&] {
                        return check_state(static_cast<int>(SyncPubState::kResponded));
                    })) {
                    LOG_ERRO("[pub] Wait response timeout, requestId: [{}]", request_id);
                    ret = static_cast<int>(MqttErrorCode::kWaitResponseTimeout);
                } else {
                    LOG_INFO("[pub] Wait response success, requestId: [{}]", request_id);
                }
            }
        } else {
            LOG_ERRO("[pub] Wait ACK timeout, requestId: [{}]", request_id);
            ret = static_cast<int>(MqttErrorCode::kWaitAckTimeout);
        }
    }
    // Only wait for response (no ACK wait)
    else if (response_timeout_ms > 0) {
        if (!sync_cv_.wait_for(lock, std::chrono::milliseconds(response_timeout_ms),
                               [&] { return check_state(static_cast<int>(SyncPubState::kResponded)); })) {
            LOG_ERRO("[pub] Wait response timeout, requestId: [{}]", request_id);
            ret = static_cast<int>(MqttErrorCode::kWaitResponseTimeout);
        } else {
            LOG_INFO("[pub] Wait response success, requestId: [{}]", request_id);
        }
    }

    // Cleanup the pending entry
    sync_results_.erase(request_id);
    return ret;
}

int CMvMQTTClient::MQTTClientPublish(const MqttMessage& msg, int response_timeout_ms,
                                     SyncPubResult* sync_result, int ack_timeout_ms) {
    LOG_INFO("[pub] MQTTClient ready to publish, topic: [{}]", msg.topic);

    // Pre-insert sync cache entry before publish to avoid race with response
    auto request_id = PrepareSyncPublish(msg, sync_result);
    bool inserted   = !request_id.empty();

    // Perform the publish
    int ret = MQTTCLIENT_SUCCESS;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (nullptr == mqtt_handle_) {
            LOG_ERRO("{}", "[pub] MQTTClient handle is nullptr");
            ret = static_cast<int>(MqttErrorCode::kClientNotCreated);
        } else if (!IsConnectedLocked()) {
            LOG_ERRO("{}", "[pub] MQTTClient is Offline");
            ret = static_cast<int>(MqttErrorCode::kOffline);
        } else {
            MQTTClient_message pubmsg = MQTTClient_message_initializer;
            // Copy payload into mutable buffer to satisfy Paho C API's void* signature
            // without const_cast. Copy cost is negligible vs. network I/O.
            std::vector<char> payload_buf(msg.payload.begin(), msg.payload.end());
            pubmsg.payload    = payload_buf.data();
            pubmsg.payloadlen = static_cast<int>(payload_buf.size());
            pubmsg.qos        = msg.qos;

            MQTTClient_deliveryToken dt;
            ret = MQTTClient_publishMessage(mqtt_handle_, msg.topic.c_str(), &pubmsg, &dt);
            if (MQTTCLIENT_SUCCESS != ret) {
                LOG_ERRO("[pub] MQTTClient_publishMessage failed, topic: [{}], qos: [{}], Error: [{}, {}]",
                         msg.topic, pubmsg.qos, ret, MQTTClient_strerror(ret));
            } else {
                LOG_INFO("[pub] MQTTClient publish success, topic: [{}], qos: [{}], token: [{}]", msg.topic,
                         pubmsg.qos, dt);
            }
        }
    }

    // On publish failure, clean up sync cache
    if (MQTTCLIENT_SUCCESS != ret) {
        if (inserted) {
            std::unique_lock<std::mutex> lock(sync_mutex_);
            sync_results_.erase(request_id);
        }
        return ret;
    }

    // Wait for sync response if applicable
    if (inserted) {
        ret = WaitForSyncResponse(request_id, ack_timeout_ms, response_timeout_ms, sync_result);
    }
    return ret;
}

int CMvMQTTClient::MQTTClientDestroy(int timeout_ms) {
    enable_reconnect_ = false;

    std::lock_guard<std::mutex> lock(mutex_);
    if (MQTTClient_isConnected(mqtt_handle_)) {
        LOG_INFO("{}", "[destroy] MQTTClient is still connected");
        auto ret = MQTTClient_disconnect(mqtt_handle_, timeout_ms);
        if (MQTTCLIENT_SUCCESS != ret) {
            LOG_ERRO("[destroy] MQTTClient_disconnect failed, Error: [{}, {}]", ret,
                     MQTTClient_strerror(ret));
            return ret;
        }
    }
    LOG_INFO("{}", "[destroy] MQTTClient disconnect success");

    MQTTClient_destroy(&mqtt_handle_);
    LOG_INFO("{}", "[destroy] MQTTClient destroy success");

    return MQTTCLIENT_SUCCESS;
}

bool CMvMQTTClient::MQTTClientIsConnected() {
    std::lock_guard<std::mutex> lock(mutex_);
    return IsConnectedLocked();
}

bool CMvMQTTClient::IsConnectedLocked() const {
    return (1 == MQTTClient_isConnected(mqtt_handle_));
}

void CMvMQTTClient::InsertSubInfo(const std::string& topic, int qos) {
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    subscriptions_[topic] = qos;
}

void CMvMQTTClient::EraseSubInfo(const std::string& topic) {
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    subscriptions_.erase(topic);
}

void CMvMQTTClient::InvokeMessageCallback(MessageArrived& msg) {
    void* ctx = nullptr;
    MessageArrivedCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cb  = on_message_;
        ctx = user_context_;
    }
    if (cb) {
        cb(std::move(msg), ctx);
    } else {
        LOG_INFO("{}", "[invoke] Message arrived callback is nullptr");
    }
}

void CMvMQTTClient::InvokeConnectionLostCallback() {
    void* ctx = nullptr;
    ConnectionLostCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cb  = on_connect_lost_;
        ctx = user_context_;
    }
    if (cb) {
        cb(ctx);
    } else {
        LOG_INFO("{}", "[invoke] Connect lost callback is nullptr");
    }
}

void CMvMQTTClient::InvokeReconnectCallback() {
    void* ctx = nullptr;
    ReconnectSuccessCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cb  = on_reconnect_;
        ctx = user_context_;
    }
    if (cb) {
        cb(ctx);
    } else {
        LOG_INFO("{}", "[invoke] Reconnect success callback is nullptr");
    }
}

bool CMvMQTTClient::UpdateSyncResult(const SyncPubResult& result) {
    bool found     = false;
    bool responded = false;
    {
        std::unique_lock<std::mutex> lock(sync_mutex_);
        auto it = sync_results_.find(result.request_id);
        if (it != sync_results_.end()) {
            found = true;
            if (it->second.sync_state & static_cast<int>(SyncPubState::kResponded)) {
                responded = true;
            } else {
                it->second.sync_state |= result.sync_state;
                it->second.qos     = result.qos;
                it->second.payload = result.payload;
                it->second.topic   = result.topic;
            }
        }
    }
    if (found) {
        if (!responded) {
            LOG_INFO("[update] Notify requestId: [{}], state: [{}]", result.request_id, result.sync_state);
            sync_cv_.notify_all();
        } else {
            LOG_INFO("[update] Unwanted notify requestId: [{}], state: [{}]", result.request_id,
                     result.sync_state);
        }
    }
    return found;
}

const std::string& CMvMQTTClient::GetSyncRequestIdPrefix() const {
    return sync_prefix_;
}

}  // namespace cosmo::network::mqtt
