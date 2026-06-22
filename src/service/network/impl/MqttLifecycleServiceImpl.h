#pragma once

// MqttLifecycleServiceImpl — MQTT lifecycle service implementation.
// Manages connection, registration, heartbeat, message routing, and auto-reconnection.
// Merged from the former CMqttAdapter + MqttLifecycleServiceImpl.

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "network/mqtt/MqttProtocol.h"
#include "service/network/IMqttLifecycle.h"
#include "util/AsyncQueue.h"
#include "util/IRequestDispatcher.h"
#include "util/PeriodicTimer.h"

namespace cosmo::network::mqtt {
class CMvMQTTClient;
struct MessageArrived;
}  // namespace cosmo::network::mqtt

namespace cosmo::service {

class MqttLifecycleServiceImpl final : public IMqttLifecycle {
public:
    using DispatcherFactory = std::function<std::unique_ptr<cosmo::IRequestDispatcher>()>;

    /// @param dispatcher_factory  Factory that creates an IRequestDispatcher
    ///                            for MQTT message routing.
    explicit MqttLifecycleServiceImpl(DispatcherFactory dispatcher_factory);
    ~MqttLifecycleServiceImpl() override;

    MqttLifecycleServiceImpl(const MqttLifecycleServiceImpl&)            = delete;
    MqttLifecycleServiceImpl& operator=(const MqttLifecycleServiceImpl&) = delete;

    // IMqttLifecycle
    bool IsMqttRegistered() override;
    bool IsMqttEnabled() override;
    void MqttStop() override;
    void MqttStart() override;

private:
    using SteadyClock = std::chrono::steady_clock;
    using TimePoint   = SteadyClock::time_point;

    // — MQTT connection lifecycle —
    void Connect(const std::string& sn, const std::string& ip, int port, int authMode,
                 const std::string& clientId, const std::string& userName, const std::string& passwd);
    void Disconnect();
    void Reconnect();

    // — MQTT client callbacks —
    void OnMessageArrived(network::mqtt::MessageArrived&& msg, void* ctx);
    void OnConnectionLost(void* ctx);
    void OnReconnectSuccess(void* ctx);

    // — Internal connection helpers —
    bool MqttClientConnect(const std::string& sn, const std::string& url, int authMode,
                           const std::string& clientId, const std::string& userName,
                           const std::string& passwd);

    // — Business logic —
    void SetReconnectTimePoint();
    void HeartBeatTimerStart();
    void RegisterBusiness();
    bool Register();
    bool HeartBeat();
    void HandleMessage(network::mqtt::MqttCommonMsgDl&& msg);
    bool SendAsyncData(const std::string& topic, const std::string& data);

    // — DNS resolution —
    static std::string ResolveHostToIP(const std::string& host);

    // — Connection thread management —
    std::thread connect_thread_;
    std::atomic_bool connect_running_{false};
    void StopConnectThread();

    // — Core components —
    DispatcherFactory dispatcher_factory_;
    std::unique_ptr<cosmo::IRequestDispatcher> dispatcher_;
    std::shared_ptr<network::mqtt::CMvMQTTClient> mqtt_client_;
    std::unique_ptr<cosmo::PeriodicTimer> timer_;
    cosmo::TaskId heartbeat_task_id_{cosmo::kInvalidTaskId};

    // — Connection state —
    std::string device_sn_;
    TimePoint time_reconnect_;
    std::string ip_;
    int port_{0};
    int auth_mode_{0};
    std::string client_id_;
    std::string user_name_;
    std::string passwd_;
    bool registered_{false};
    uint32_t heartbeat_failed_count_{0};

    // — Async work queues —
    cosmo::AsyncQueue<std::string> connect_queue_;
    cosmo::AsyncQueue<network::mqtt::MqttCommonMsgDl> message_queue_;
};

}  // namespace cosmo::service
