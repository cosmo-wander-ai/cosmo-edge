// MqttLifecycleServiceImpl — MQTT lifecycle service.
// Merged from MqttLifecycleServiceImpl + CMqttAdapter.

#include "service/network/impl/MqttLifecycleServiceImpl.h"

#include <arpa/inet.h>
#include <event2/dns.h>
#include <event2/event.h>
#include <event2/util.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <chrono>
#include <memory>
#include <thread>

#include "network/mqtt/MqttClient.h"
#include "network/mqtt/MqttProtocol.h"
#include "network/mqtt/MqttTypes.h"
#include "nlohmann/json.hpp"
#include "service/detail/ServiceRegistry.h"
#include "service/system/IAppInfoService.h"
#include "service/system/IConfigNetworkService.h"
#include "service/system/IConfigReadService.h"
#include "service/system/IDeviceHardware.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"
#include "util/SafeParse.h"
#include "util/UuidUtil.h"
#include "util/Version.h"

namespace chrono = std::chrono;

namespace cosmo::service {

namespace mqtt = cosmo::network::mqtt;

// MQTT topic prefixes
static const std::string k_device_to_platform = "/d2p/aibox";
static const std::string k_platform_to_device = "/p2d/aibox";

// Reconnect retry interval in seconds
static constexpr unsigned kReconnectRetrySec = 10;
// DNS resolution timeout
static constexpr auto kDnsTimeout                 = chrono::seconds(5);
static constexpr auto kDnsPollInterval            = chrono::milliseconds(50);
static constexpr int kDnsCancelDrainMaxIterations = 4;
// Heartbeat failure threshold before reconnect
static constexpr uint32_t kHeartbeatFailMax = 5;
// Sync publish ACK timeout (ms) for registration
static constexpr int k_register_ack_timeout_ms = 5000;
// Sync publish ACK timeout (ms) for heartbeat
static constexpr int k_heartbeat_ack_timeout_ms = 2000;
// Retry interval for registration (seconds)
static constexpr int k_register_repeat_interval_sec = 10;
// Heartbeat interval (seconds)
static constexpr int k_heart_beat_repeat_interval_sec = 30;

namespace {

    struct EventBaseDeleter {
        void operator()(event_base* base) const {
            if (base) {
                event_base_free(base);
            }
        }
    };

    struct DnsBaseDeleter {
        void operator()(evdns_base* base) const {
            if (base) {
                evdns_base_free(base, 0);
            }
        }
    };

    struct DnsResolutionResult {
        ~DnsResolutionResult() {
            if (addresses) {
                evutil_freeaddrinfo(addresses);
            }
        }

        bool completed{false};
        int error{EVUTIL_EAI_FAIL};
        evutil_addrinfo* addresses{nullptr};
    };

}  // namespace

bool detail::CancelAndDrainDnsRequest(event_base* event_base_ptr, evdns_getaddrinfo_request* request,
                                      const bool* completed) noexcept {
    if (event_base_ptr == nullptr || request == nullptr || completed == nullptr) {
        return false;
    }

    evdns_getaddrinfo_cancel(request);
    for (int iteration = 0; iteration < kDnsCancelDrainMaxIterations && !*completed; ++iteration) {
        // Cancellation queues a deferred callback. NONBLOCK keeps Stop bounded;
        // ONCE prevents unrelated callbacks from keeping this private loop alive.
        const int loop_status = event_base_loop(event_base_ptr, EVLOOP_NONBLOCK | EVLOOP_ONCE);
        if (loop_status != 0) {
            return false;
        }
    }
    return *completed;
}

// ── Construction / Destruction ──────────────────────────────────────────

MqttLifecycleServiceImpl::MqttLifecycleServiceImpl(DispatcherFactory dispatcher_factory)
    : dispatcher_factory_(std::move(dispatcher_factory)),
      mqtt_client_(std::make_shared<mqtt::CMvMQTTClient>(std::string())),
      timer_(std::make_unique<cosmo::PeriodicTimer>("MQTT CLIENT")),
      connect_queue_("Mqtt Connect", 3),
      message_queue_("Mqtt Message Handle", 100) {
    timer_->Start();

    mqtt_client_->MQTTClientSetCallbacks(
        mqtt_client_.get(),
        [this](mqtt::MessageArrived&& msg, void* usr) { OnMessageArrived(std::move(msg), usr); },
        [this](void* usr) { OnConnectionLost(usr); }, [this](void* usr) { OnReconnectSuccess(usr); });

    connect_queue_.SetProcessor([this](std::string&& /*data*/) {
        if (shutting_down_.load(std::memory_order_acquire) ||
            !desired_running_.load(std::memory_order_acquire)) {
            return;
        }
        Reconnect();
    });

    message_queue_.SetProcessor([this](mqtt::MqttCommonMsgDl&& data) { HandleMessage(std::move(data)); });
}

MqttLifecycleServiceImpl::~MqttLifecycleServiceImpl() {
    MqttLifecycleServiceImpl::MqttShutdown();
    LOG_INFO("{}", "MqttLifecycleServiceImpl destroyed");
}

void MqttLifecycleServiceImpl::MqttShutdown() {
    std::lock_guard<std::mutex> shutdown_lock(shutdown_mtx_);
    if (shutdown_complete_) {
        return;
    }

    shutting_down_.store(true, std::memory_order_release);
    desired_running_.store(false, std::memory_order_release);

    // Prevent Paho callbacks and queue workers from entering this object while
    // its connection state and dispatcher are being torn down.
    mqtt_client_->MQTTClientSetCallbacks(nullptr, nullptr, nullptr, nullptr);
    {
        std::lock_guard<std::recursive_mutex> lifecycle_lock(lifecycle_mtx_);
        Disconnect();
    }
    connect_queue_.Stop();
    message_queue_.Stop();
    connect_queue_.stop();
    message_queue_.stop();
    if (heartbeat_task_id_ != cosmo::kInvalidTaskId) {
        timer_->Cancel(heartbeat_task_id_);
        heartbeat_task_id_ = cosmo::kInvalidTaskId;
    }
    if (timer_) {
        timer_->Destroy();
        timer_.reset();
    }
    dispatcher_.reset();
    shutdown_complete_ = true;
}

// ── IMqttLifecycle interface ────────────────────────────────────────────

bool MqttLifecycleServiceImpl::IsMqttRegistered() {
    return mqtt_client_->MQTTClientIsConnected() && registered_.load(std::memory_order_acquire);
}

bool MqttLifecycleServiceImpl::IsMqttEnabled() {
    return mqtt_client_->MQTTClientIsConnected();
}

void MqttLifecycleServiceImpl::MqttStop() {
    std::lock_guard<std::recursive_mutex> lifecycle_lock(lifecycle_mtx_);
    if (shutting_down_.load(std::memory_order_acquire)) {
        return;
    }
    LOG_INFO("{}", "StopMqttServer");
    desired_running_.store(false, std::memory_order_release);
    Disconnect();
}

void MqttLifecycleServiceImpl::MqttStart() {
    std::lock_guard<std::recursive_mutex> lifecycle_lock(lifecycle_mtx_);
    if (shutting_down_.load(std::memory_order_acquire)) {
        return;
    }
    desired_running_.store(false, std::memory_order_release);
    // Lazily create the dispatcher on first use
    if (!dispatcher_ && dispatcher_factory_) {
        dispatcher_ = dispatcher_factory_();
    }

    std::string url;
    int port      = 1883;
    int auth_mode = 0;
    std::string client_id;
    std::string user_name;
    std::string passwd;
    auto run_mode = ServiceRegistry::Instance().Get<IConfigReadService>().GetRunMode();
    if (RunMode::RunModeStandAlone == run_mode) {
        auto mqtt_param = ServiceRegistry::Instance().Get<IConfigNetworkService>().GetMqttParam();
        if (!mqtt_param.enable) {
            desired_running_.store(false, std::memory_order_release);
            Disconnect();
            return;
        }
        url       = mqtt_param.url;
        port      = mqtt_param.port;
        auth_mode = mqtt_param.authMode;
        client_id = mqtt_param.clientId;
        user_name = mqtt_param.userName;
        passwd    = mqtt_param.passwd;
        LOG_INFO("RunMode StandAlone MQTT port:{}", port);
    } else if (RunMode::RunModeIotNetwork == run_mode) {
        auto iot_param = ServiceRegistry::Instance().Get<IConfigNetworkService>().GetIotNetworkParam();
        url            = iot_param.mqttIp;
        port           = iot_param.mqttPort;
        LOG_INFO("RunMode IOTNetwork MQTT port:{}", port);
    } else {
        LOG_ERRO("RunMode ({}) Un-Support", run_mode);
        Disconnect();
        return;
    }
    desired_running_.store(true, std::memory_order_release);
    Disconnect();
    std::string device_sn = ServiceRegistry::Instance().Get<IDeviceHardware>().GetDevSn();
    Connect(device_sn, url, port, auth_mode, client_id, user_name, passwd);
}

// ── Connection lifecycle ────────────────────────────────────────────────

void MqttLifecycleServiceImpl::Connect(const std::string& sn, const std::string& ip, int port, int auth_mode,
                                       const std::string& client_id, const std::string& user_name,
                                       const std::string& passwd) {
    device_sn_  = sn;
    ip_         = ip;
    port_       = port;
    registered_ = false;
    auth_mode_  = auth_mode;
    client_id_  = client_id;
    user_name_  = user_name;
    passwd_     = passwd;

    StopConnectThread();

    LOG_INFO("{}", "Now Start Loop Connection.");
    connect_running_.store(true, std::memory_order_release);
    connect_thread_ = std::thread([this, sn, ip, port, auth_mode, client_id, user_name, passwd]() {
        while (connect_running_.load(std::memory_order_acquire)) {
            LOG_INFO("{}", "MQTT Loop Connection Thread...");
            std::string url = ResolveHostToIP(ip);
            if (!connect_running_.load(std::memory_order_acquire)) {
                break;
            }
            if (url.empty()) {
                LOG_ERRO("Failed to resolve host [{}], will not connect MQTTServer", ip);
            } else if (MqttClientConnect(sn, url + ":" + std::to_string(port), auth_mode, client_id,
                                         user_name, passwd)) {
                LOG_INFO("{}", "MQTT Loop Connection Success.");
                break;
            }

            if (WaitForConnectStop(chrono::seconds(kReconnectRetrySec))) {
                break;
            }
        }
        if (!connect_running_.load(std::memory_order_acquire)) {
            LOG_INFO("{}", "MQTT Loop Connection Stop By Force.");
        }
    });
}

void MqttLifecycleServiceImpl::Disconnect() {
    StopConnectThread();
    registered_.store(false, std::memory_order_release);
    if (heartbeat_task_id_ != cosmo::kInvalidTaskId) {
        timer_->Cancel(heartbeat_task_id_);
        heartbeat_task_id_ = cosmo::kInvalidTaskId;
    }
    int ret = mqtt_client_->MQTTClientDestroy(50);
    LOG_INFO("Stop Thread and Destroy MqttClient [{}]", ret);
}

void MqttLifecycleServiceImpl::Reconnect() {
    std::lock_guard<std::recursive_mutex> lifecycle_lock(lifecycle_mtx_);
    if (shutting_down_.load(std::memory_order_acquire) || !desired_running_.load(std::memory_order_acquire)) {
        return;
    }
    Disconnect();
    LOG_INFO("Reconnect To IP:{} Port:{}", ip_, port_);
    Connect(device_sn_, ip_, port_, auth_mode_, client_id_, user_name_, passwd_);
}

void MqttLifecycleServiceImpl::StopConnectThread() {
    connect_running_.store(false, std::memory_order_release);
    connect_wait_cv_.notify_all();
    if (connect_thread_.joinable()) {
        connect_thread_.join();
    }
}

bool MqttLifecycleServiceImpl::WaitForConnectStop(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(connect_wait_mtx_);
    return connect_wait_cv_.wait_for(lock, timeout,
                                     [this]() { return !connect_running_.load(std::memory_order_acquire); });
}

// ── MQTT client callbacks ───────────────────────────────────────────────

void MqttLifecycleServiceImpl::OnMessageArrived(mqtt::MessageArrived&& msg, void* /*ctx*/) {
    if (shutting_down_.load(std::memory_order_acquire) || !desired_running_.load(std::memory_order_acquire)) {
        return;
    }
    mqtt::MqttCommonMsgDl dl_msg;
    if (!cosmo::util::DecodeJson(msg.payload, dl_msg)) {
        LOG_WARN("Receive {} Analysis Failed.", msg.payload);
        return;
    }
    LOG_INFO("Receive {} .", dl_msg.head.action);
    message_queue_.Insert(dl_msg);
}

void MqttLifecycleServiceImpl::OnConnectionLost(void* /*ctx*/) {
    registered_.store(false, std::memory_order_release);
    LOG_INFO("{}", "Mqtt Client Connect Lost");
}

void MqttLifecycleServiceImpl::OnReconnectSuccess(void* /*ctx*/) {
    LOG_INFO("{}", "Mqtt Client Reconnect Success");
    if (!shutting_down_.load(std::memory_order_acquire) && desired_running_.load(std::memory_order_acquire)) {
        connect_queue_.Insert("reconnect");
    }
}

// ── Internal connection ─────────────────────────────────────────────────

bool MqttLifecycleServiceImpl::MqttClientConnect(const std::string& sn, const std::string& url, int auth_mode,
                                                 const std::string& client_id, const std::string& user_name,
                                                 const std::string& passwd) {
    if (!mqtt_client_) {
        LOG_ERRO("{}", "MQTTClient Handle is NULL");
        return false;
    }
    if (!connect_running_.load(std::memory_order_acquire)) {
        return false;
    }

    mqtt::MqttConnectOptions opts;
    opts.server_uri            = url;
    opts.keep_alive_interval_s = 20;
    opts.max_inflight_msgs     = 100;
    opts.device_type           = "box";

    if (auth_mode) {
        opts.auth_type = mqtt::MqttAuthType::kNormal;
        opts.auth_key  = "cwai";
        opts.client_id = client_id;
        opts.username  = user_name;
        opts.password  = passwd;
    } else {
        opts.auth_type = mqtt::MqttAuthType::kMvIot;
        opts.auth_key  = "cwai";
        opts.client_id = sn;
        opts.username  = "aibox::" + sn;
    }
    LOG_INFO("{}", "MLINK CONNECT requested");
    opts.device_sn = sn;

    LOG_INFO("Before MQTTClientCreate, MQTTClient State: [{}]", mqtt_client_->MQTTClientIsConnected());
    if (mqtt_client_->MQTTClientCreate(opts)) {
        LOG_ERRO("MQTTClientCreate Failed, SN: [{}]", sn);
        return false;
    }
    if (!connect_running_.load(std::memory_order_acquire)) {
        return false;
    }

    LOG_INFO("MQTTClientCreate Success: [{}]", mqtt_client_->MQTTClientIsConnected());
    return RegisterBusiness();
}

// ── DNS resolution ──────────────────────────────────────────────────────

std::string MqttLifecycleServiceImpl::ResolveHostToIP(const std::string& host) {
    if (host.empty()) {
        return {};
    }

    in_addr numeric_address{};
    if (inet_pton(AF_INET, host.c_str(), &numeric_address) == 1) {
        return host;
    }

    std::unique_ptr<event_base, EventBaseDeleter> event_base_ptr(event_base_new());
    if (!event_base_ptr) {
        LOG_ERRO("Failed to create DNS event base for host [{}]", host);
        return {};
    }
    std::unique_ptr<evdns_base, DnsBaseDeleter> dns_base_ptr(evdns_base_new(event_base_ptr.get(), 1));
    if (!dns_base_ptr) {
        LOG_ERRO("Failed to create DNS resolver for host [{}]", host);
        return {};
    }

    evutil_addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    DnsResolutionResult result;
    auto callback = [](int error, evutil_addrinfo* addresses, void* context) {
        auto* resolution      = static_cast<DnsResolutionResult*>(context);
        resolution->completed = true;
        resolution->error     = error;
        resolution->addresses = addresses;
    };
    auto* request = evdns_getaddrinfo(dns_base_ptr.get(), host.c_str(), nullptr, &hints, callback, &result);

    const auto deadline = SteadyClock::now() + kDnsTimeout;
    bool loop_failed    = false;
    while (!result.completed && connect_running_.load(std::memory_order_acquire) &&
           SteadyClock::now() < deadline) {
        if (event_base_loop(event_base_ptr.get(), EVLOOP_NONBLOCK) < 0) {
            loop_failed = true;
            break;
        }
        // event_base_loop may synchronously run the DNS callback above.
        // cppcheck-suppress knownConditionTrueFalse
        if (!result.completed) {
            (void)WaitForConnectStop(kDnsPollInterval);
        }
    }

    const bool stopped = !connect_running_.load(std::memory_order_acquire);
    if (!result.completed && request) {
        if (!detail::CancelAndDrainDnsRequest(event_base_ptr.get(), request, &result.completed)) {
            LOG_ERRO("Failed to drain cancelled DNS request for host [{}]", host);
            loop_failed = true;
        }
    }
    if (stopped) {
        return {};
    }
    if (loop_failed) {
        LOG_ERRO("DNS event loop failed for host [{}]", host);
        return {};
    }
    if (!result.completed || result.error == EVUTIL_EAI_CANCEL) {
        LOG_ERRO("DNS resolution timeout for host [{}] after {}s", host, kDnsTimeout.count());
        return {};
    }
    if (result.error != 0 || !result.addresses) {
        LOG_ERRO("DNS resolution failed for host [{}]: {}", host, evutil_gai_strerror(result.error));
        return {};
    }

    for (auto* address = result.addresses; address != nullptr; address = address->ai_next) {
        if (address->ai_family != AF_INET || !address->ai_addr) {
            continue;
        }
        char ip_str[INET_ADDRSTRLEN] = {};
        auto* socket_address         = reinterpret_cast<sockaddr_in*>(address->ai_addr);
        if (inet_ntop(AF_INET, &socket_address->sin_addr, ip_str, sizeof(ip_str))) {
            return ip_str;
        }
    }

    LOG_ERRO("DNS resolution for host [{}] returned no IPv4 address", host);
    return {};
}

// ── Business logic ──────────────────────────────────────────────────────

void MqttLifecycleServiceImpl::HeartBeatTimerStart() {
    if (heartbeat_task_id_ != cosmo::kInvalidTaskId) {
        LOG_INFO("{}", "Cancel HeartBeat Timer");
        timer_->Cancel(heartbeat_task_id_);
        heartbeat_task_id_ = cosmo::kInvalidTaskId;
    }

    if constexpr (k_heart_beat_repeat_interval_sec > 0) {
        heartbeat_task_id_ =
            timer_->Schedule([this]() { return HeartBeat(); }, k_heart_beat_repeat_interval_sec * 1000);
    } else {
        registered_ = false;
    }
}

bool MqttLifecycleServiceImpl::RegisterBusiness() {
    if (!connect_running_.load(std::memory_order_acquire)) {
        return false;
    }
    // Subscribe to device-specific topics
    std::string topic = k_platform_to_device + "/" + device_sn_;
    if (mqtt_client_->MQTTClientSubscribe(topic, 1)) {
        LOG_ERRO("MQTTClientSubscribe Topic [{}] Failed", topic);
        return false;
    }

    topic = k_platform_to_device + "/heartbeat/" + device_sn_;
    if (mqtt_client_->MQTTClientSubscribe(topic, 1)) {
        LOG_WARN("MQTTClientSubscribe Topic [{}] Failed", topic);
    }

    while (connect_running_.load(std::memory_order_acquire) && !Register()) {
        if (WaitForConnectStop(chrono::seconds(k_register_repeat_interval_sec))) {
            LOG_WARN("{}", "MQTT Status is Released, Stop Register");
            return false;
        }
    }
    if (!connect_running_.load(std::memory_order_acquire)) {
        LOG_WARN("{}", "MQTT Status is Released, Stop Register");
        return false;
    }

    LOG_INFO("{}", "MQTT Register Ok, Start HeartBeat Timer.");
    HeartBeatTimerStart();
    return true;
}

bool MqttLifecycleServiceImpl::Register() {
    heartbeat_failed_count_ = 0;

    mqtt::MqttMessage pub_msg;
    pub_msg.qos   = 1;
    pub_msg.topic = k_device_to_platform;

    mqtt::MqttMsgOnlineSend sendmsg;
    sendmsg.head.request_id = cosmo::util::GenerateUUID();
    sendmsg.head.device_sn  = device_sn_;
    sendmsg.head.msg_type   = "register";

    sendmsg.body.dev_id          = device_sn_;
    sendmsg.body.ai_host_version = std::string(cosmo::util::GetVersion());
    sendmsg.body.engine_type =
        cosmo::service::ServiceRegistry::Instance().Get<service::IAppInfoService>().GetEngineType();
    sendmsg.body.device_model =
        cosmo::service::ServiceRegistry::Instance().Get<service::IDeviceHardware>().GetDevModel();
    sendmsg.body.dev_type = 2;

    pub_msg.payload = nlohmann::json(sendmsg).dump();
    mqtt::SyncPubResult sync_result;
    if (mqtt_client_->MQTTClientPublish(pub_msg, 0, &sync_result, k_register_ack_timeout_ms)) {
        LOG_WARN("Publish [Register] Failed, Payload: [{}]", pub_msg.payload);
        return false;
    }

    registered_ = true;
    LOG_INFO("{}", "[Register] Success");
    return true;
}

bool MqttLifecycleServiceImpl::HeartBeat() {
    std::lock_guard<std::recursive_mutex> lifecycle_lock(lifecycle_mtx_);
    if (shutting_down_.load(std::memory_order_acquire) || !desired_running_.load(std::memory_order_acquire)) {
        return true;
    }
    mqtt::MqttMessage pub_msg;
    pub_msg.qos   = 1;
    pub_msg.topic = k_device_to_platform + "/heartbeat";

    mqtt::MqttMsgHeartBeatSend sendmsg;
    sendmsg.head.request_id = cosmo::util::GenerateUUID();
    sendmsg.head.device_sn  = device_sn_;
    sendmsg.head.msg_type   = "heartbeat";

    // Directly call IAppInfoService instead of routing through ApiRouter/MessageHandler
    cosmo::MsgInfoRecv data;
    std::error_condition errc;
    auto ret_data = ServiceRegistry::Instance().Get<IAppInfoService>().GetSystemOverviewInfo(data, errc);

    sendmsg.body.dev_id            = device_sn_;
    sendmsg.body.host_status       = ret_data.hostStatus;
    sendmsg.body.custom_score      = cosmo::util::ParseFloat(ret_data.customScore);
    sendmsg.body.cpu_usage         = ret_data.cpuUsage;
    sendmsg.body.mem_total         = ret_data.memTotal;
    sendmsg.body.mem_available     = ret_data.memAvailable;
    sendmsg.body.gpu_usage         = ret_data.gpuUsage;
    sendmsg.body.gpu_mem_total     = ret_data.gpuMemTotal;
    sendmsg.body.gpu_mem_available = ret_data.gpuMemAvailable;
    sendmsg.body.disk_total        = ret_data.diskTotal;
    sendmsg.body.disk_available    = ret_data.diskAvailable;

    pub_msg.payload = nlohmann::json(sendmsg).dump();
    mqtt::SyncPubResult sync_result;
    if (mqtt_client_->MQTTClientPublish(pub_msg, 0, &sync_result, k_heartbeat_ack_timeout_ms)) {
        heartbeat_failed_count_ += 1;
        if (heartbeat_failed_count_ >= kHeartbeatFailMax) {
            registered_ = false;
            LOG_INFO("Heartbeat failed too many times ({}), reconnecting", heartbeat_failed_count_);
            Reconnect();
            heartbeat_failed_count_ = 0;
            return true;  // Stop timer
        }
        LOG_WARN("Publish HeartBeat Failed. FailCount:{}", heartbeat_failed_count_);
        return false;
    }
    heartbeat_failed_count_ = 0;
    return false;
}

// ── Message handling ────────────────────────────────────────────────────

void MqttLifecycleServiceImpl::HandleMessage(mqtt::MqttCommonMsgDl&& msg) {
    std::lock_guard<std::recursive_mutex> lifecycle_lock(lifecycle_mtx_);
    if (shutting_down_.load(std::memory_order_acquire) || !desired_running_.load(std::memory_order_acquire)) {
        return;
    }
    if (!IsMqttRegistered()) {
        LOG_WARN("Drop MQTT request before device registration. Action:{}", msg.head.action);
        return;
    }

    if (msg.head.device_sn != device_sn_) {
        LOG_WARN("RECV MSG DeviceSn:{} Local Device:{} Action:{}", msg.head.device_sn, device_sn_,
                 msg.head.action);
        return;
    }

    if (msg.head.msg_type != "request") {
        LOG_WARN("RECV MSG Type {} Is Not request. Action:{}", msg.head.msg_type, msg.head.action);
        return;
    }

    chrono::steady_clock::time_point time_start = chrono::steady_clock::now();
    mqtt::MqttCommonMsgUl response;
    response.head.request_id = msg.head.request_id;
    response.head.action     = msg.head.action;
    response.head.device_sn  = msg.head.device_sn;
    response.head.msg_type   = "response";
    RequestDispatchContext context;
    context.uri       = msg.head.action;
    context.transport = RequestTransport::kMqtt;
    dispatcher_->DispatchRequest(context, msg.body, response.body);

    std::string res_string;
    (void)cosmo::util::EncodeJson(response, res_string);

    LOG_DEBUG("Handle:{} elapsed_ms:{} response_bytes:{}", msg.head.action,
              chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - time_start).count(),
              res_string.size());

    SendAsyncData(k_device_to_platform, res_string);
}

bool MqttLifecycleServiceImpl::SendAsyncData(const std::string& topic, const std::string& data) {
    if (!IsMqttRegistered()) {
        LOG_ERRO("{}", "Mqtt Is Offline");
        return false;
    }

    mqtt::MqttMessage msg;
    msg.qos     = 1;
    msg.topic   = topic;
    msg.payload = data;
    return mqtt_client_->MQTTClientPublish(msg) == 0;
}

}  // namespace cosmo::service
