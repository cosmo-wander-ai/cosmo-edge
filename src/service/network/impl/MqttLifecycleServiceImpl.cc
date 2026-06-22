// MqttLifecycleServiceImpl — MQTT lifecycle service.
// Merged from MqttLifecycleServiceImpl + CMqttAdapter.

#include "service/network/impl/MqttLifecycleServiceImpl.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <chrono>
#include <future>
#include <thread>

#include "network/mqtt/MqttClient.h"
#include "network/mqtt/MqttProtocol.h"
#include "network/mqtt/MqttTypes.h"
#include "nlohmann/json.hpp"
#include "service/detail/ServiceRegistry.h"
#include "service/system/IAppInfoService.h"
#include "service/system/IConfigNetworkService.h"
#include "service/system/IConfigReadService.h"
#include "service/system/IDeviceInfoService.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"
#include "util/SafeParse.h"
#include "util/TimingConstants.h"
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
static constexpr int k_dns_timeout_sec = 5;
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

// ── Construction / Destruction ──────────────────────────────────────────

MqttLifecycleServiceImpl::MqttLifecycleServiceImpl(DispatcherFactory dispatcher_factory)
    : dispatcher_factory_(std::move(dispatcher_factory)),
      mqtt_client_(std::make_shared<mqtt::CMvMQTTClient>(std::string())),
      timer_(std::make_unique<cosmo::PeriodicTimer>("MQTT CLIENT")),
      connect_queue_("Mqtt Connect", 3),
      message_queue_("Mqtt Message Handle", 100) {
    timer_->Start();

    time_reconnect_ = TimePoint();
    mqtt_client_->MQTTClientSetCallbacks(
        mqtt_client_.get(),
        [this](mqtt::MessageArrived&& msg, void* usr) { OnMessageArrived(std::move(msg), usr); },
        [this](void* usr) { OnConnectionLost(usr); }, [this](void* usr) { OnReconnectSuccess(usr); });

    connect_queue_.SetProcessor([this](std::string&& /*data*/) {
        Reconnect();
        SetReconnectTimePoint();
    });

    message_queue_.SetProcessor([this](mqtt::MqttCommonMsgDl&& data) { HandleMessage(std::move(data)); });
}

MqttLifecycleServiceImpl::~MqttLifecycleServiceImpl() {
    if (heartbeat_task_id_ != cosmo::kInvalidTaskId) {
        timer_->Cancel(heartbeat_task_id_);
        heartbeat_task_id_ = cosmo::kInvalidTaskId;
    }
    if (timer_) {
        timer_->Destroy();
    }
    Disconnect();
    LOG_INFO("{}", "MqttLifecycleServiceImpl destroyed");
}

// ── IMqttLifecycle interface ────────────────────────────────────────────

bool MqttLifecycleServiceImpl::IsMqttRegistered() {
    return mqtt_client_->MQTTClientIsConnected() && registered_;
}

bool MqttLifecycleServiceImpl::IsMqttEnabled() {
    return mqtt_client_->MQTTClientIsConnected();
}

void MqttLifecycleServiceImpl::MqttStop() {
    LOG_INFO("{}", "StopMqttServer");
    Disconnect();
}

void MqttLifecycleServiceImpl::MqttStart() {
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
            return;
        }
        url       = mqtt_param.url;
        port      = mqtt_param.port;
        auth_mode = mqtt_param.authMode;
        client_id = mqtt_param.clientId;
        user_name = mqtt_param.userName;
        passwd    = mqtt_param.passwd;
        LOG_INFO("RunMode StandAlone {}:{}", url, port);
    } else if (RunMode::RunModeIotNetwork == run_mode) {
        auto iot_param = ServiceRegistry::Instance().Get<IConfigNetworkService>().GetIotNetworkParam();
        url            = iot_param.mqttIp;
        port           = iot_param.mqttPort;
        LOG_INFO("RunMode IOTNetwork {}:{}", url, port);
    } else {
        LOG_ERRO("RunMode ({}) Un-Support", run_mode);
        return;
    }
    if (IsMqttEnabled()) {
        Disconnect();
    }
    std::string device_sn = ServiceRegistry::Instance().Get<IDeviceInfoService>().GetDevSn();
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
        size_t attempt_count = 0;
        while (connect_running_.load(std::memory_order_acquire)) {
            // Retry every kReconnectRetrySec seconds
            if (0 == attempt_count++ % kReconnectRetrySec) {
                LOG_INFO("{}", "MQTT Loop Connection Thread...");
                std::string url = ResolveHostToIP(ip);
                if (url.empty()) {
                    LOG_ERRO("Failed to resolve host [{}], will not connect MQTTServer", ip);
                    continue;
                }

                if (MqttClientConnect(sn, url + ":" + std::to_string(port), auth_mode, client_id, user_name,
                                      passwd)) {
                    LOG_INFO("{}", "MQTT Loop Connection Success.");
                    break;
                }
            }
            std::this_thread::sleep_for(timing::kOneSecondInterval);
        }
        if (!connect_running_.load(std::memory_order_acquire)) {
            LOG_INFO("{}", "MQTT Loop Connection Stop By Force.");
        }
    });
}

void MqttLifecycleServiceImpl::Disconnect() {
    StopConnectThread();
    int ret = mqtt_client_->MQTTClientDestroy(50);
    LOG_INFO("Stop Thread and Destroy MqttClient [{}]", ret);
}

void MqttLifecycleServiceImpl::Reconnect() {
    if (IsMqttEnabled()) {
        Disconnect();
    }
    LOG_INFO("Reconnect To IP:{} Port:{}", ip_, port_);
    Connect(device_sn_, ip_, port_, auth_mode_, client_id_, user_name_, passwd_);
}

void MqttLifecycleServiceImpl::StopConnectThread() {
    connect_running_.store(false, std::memory_order_release);
    if (connect_thread_.joinable()) {
        connect_thread_.join();
    }
}

// ── MQTT client callbacks ───────────────────────────────────────────────

void MqttLifecycleServiceImpl::OnMessageArrived(mqtt::MessageArrived&& msg, void* /*ctx*/) {
    mqtt::MqttCommonMsgDl dl_msg;
    if (!cosmo::util::DecodeJson(msg.payload, dl_msg)) {
        LOG_WARN("Receive {} Analysis Failed.", msg.payload);
        return;
    }
    LOG_INFO("Receive {} .", dl_msg.head.action);
    message_queue_.Insert(dl_msg);
}

void MqttLifecycleServiceImpl::OnConnectionLost(void* /*ctx*/) {
    LOG_INFO("{}", "Mqtt Client Connect Lost");
}

void MqttLifecycleServiceImpl::OnReconnectSuccess(void* /*ctx*/) {
    LOG_INFO("{}", "Mqtt Client Reconnect Success");
    connect_queue_.Insert("reconnect");
}

// ── Internal connection ─────────────────────────────────────────────────

bool MqttLifecycleServiceImpl::MqttClientConnect(const std::string& sn, const std::string& url, int auth_mode,
                                                 const std::string& client_id, const std::string& user_name,
                                                 const std::string& passwd) {
    if (!mqtt_client_) {
        LOG_ERRO("{}", "MQTTClient Handle is NULL");
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
    LOG_INFO("MLINK CONNECT To {}", url);
    opts.device_sn = sn;

    LOG_INFO("Before MQTTClientCreate, MQTTClient State: [{}]", mqtt_client_->MQTTClientIsConnected());
    if (mqtt_client_->MQTTClientCreate(opts)) {
        LOG_ERRO("MQTTClientCreate Failed, SN: [{}], serverURI: [{}]", sn, url);
        return false;
    }

    LOG_INFO("MQTTClientCreate Success: [{}]", mqtt_client_->MQTTClientIsConnected());
    RegisterBusiness();
    return true;
}

// ── DNS resolution ──────────────────────────────────────────────────────

std::string MqttLifecycleServiceImpl::ResolveHostToIP(const std::string& host) {
    auto future = std::async(std::launch::async, [host]() -> std::string {
        struct addrinfo hints {};
        hints.ai_family   = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        struct addrinfo* result = nullptr;
        int ret                 = getaddrinfo(host.c_str(), nullptr, &hints, &result);
        if (ret != 0 || result == nullptr) {
            LOG_ERRO("getaddrinfo failed for host [{}]: {}", host, gai_strerror(ret));
            return "";
        }

        char ip_str[INET6_ADDRSTRLEN] = {};
        auto* addr                    = reinterpret_cast<struct sockaddr_in*>(result->ai_addr);
        inet_ntop(AF_INET, &addr->sin_addr, ip_str, sizeof(ip_str));
        freeaddrinfo(result);
        return std::string(ip_str);
    });

    if (future.wait_for(std::chrono::seconds(k_dns_timeout_sec)) == std::future_status::ready) {
        return future.get();
    }

    LOG_ERRO("DNS resolution timeout for host [{}] after {}s", host, k_dns_timeout_sec);
    return "";
}

// ── Business logic ──────────────────────────────────────────────────────

void MqttLifecycleServiceImpl::SetReconnectTimePoint() {
    time_reconnect_ = SteadyClock::now();
}

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

void MqttLifecycleServiceImpl::RegisterBusiness() {
    // Subscribe to device-specific topics
    std::string topic = k_platform_to_device + "/" + device_sn_;
    if (mqtt_client_->MQTTClientSubscribe(topic, 1)) {
        LOG_ERRO("MQTTClientSubscribe Topic [{}] Failed", topic);
        return;
    }

    topic = k_platform_to_device + "/heartbeat/" + device_sn_;
    if (mqtt_client_->MQTTClientSubscribe(topic, 1)) {
        LOG_WARN("MQTTClientSubscribe Topic [{}] Failed", topic);
    }

    while (!Register()) {
        int wait_count = 0;
        while (connect_running_.load(std::memory_order_acquire) &&
               (wait_count < k_register_repeat_interval_sec)) {
            std::this_thread::sleep_for(timing::kOneSecondInterval);
            wait_count++;
        }
        if (!connect_running_.load(std::memory_order_acquire)) {
            LOG_WARN("{}", "MQTT Status is Released, Stop Register");
            return;
        }
    }

    LOG_INFO("{}", "MQTT Register Ok, Start HeartBeat Timer.");
    HeartBeatTimerStart();
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
        cosmo::service::ServiceRegistry::Instance().Get<service::IDeviceInfoService>().GetDevModel();
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
    // Use IRequestDispatcher interface with empty mtk (MQTT routes skip auth validation)
    dispatcher_->DispatchRequest(msg.head.action, /*mtk=*/"", msg.body, response.body);

    std::string res_string;
    (void)cosmo::util::EncodeJson(response, res_string);

    LOG_INFO("Handle:{} With {} Ms Rsp: {:.4096}{}", msg.head.action,
             chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - time_start).count(),
             res_string, res_string.size() > 4096 ? " ..." : "");

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
