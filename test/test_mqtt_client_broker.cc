#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "catch_amalgamated.hpp"
#include "mock/MockAppInfoService.h"
#include "mock/MockConfigNetworkService.h"
#include "mock/MockConfigReadService.h"
#include "mock/MockDeviceInfoService.h"
#include "network/mqtt/MqttClient.h"
#include "nlohmann/json.hpp"
#include "service/network/impl/MqttLifecycleServiceImpl.h"
#include "support/ScopedServiceOverride.h"
#include "util/UuidUtil.h"

namespace cosmo::test {
namespace {

    namespace mqtt = cosmo::network::mqtt;
    using Json     = nlohmann::json;
    using namespace std::chrono_literals;

    struct BrokerAddress {
        std::string host;
        int port;
    };

    BrokerAddress ReadBrokerAddress() {
        const char* port_text = std::getenv("COSMO_MQTT_TEST_PORT");
        if (port_text == nullptr || *port_text == '\0') {
            SKIP("Set COSMO_MQTT_TEST_PORT to run against mqtt-client-test-broker.js");
        }
        char* end       = nullptr;
        const long port = std::strtol(port_text, &end, 10);
        REQUIRE(end != port_text);
        REQUIRE(*end == '\0');
        REQUIRE(port > 0);
        REQUIRE(port <= 65535);
        const char* host = std::getenv("COSMO_MQTT_TEST_HOST");
        return {host != nullptr && *host != '\0' ? host : "127.0.0.1", static_cast<int>(port)};
    }

    class MessageInbox {
    public:
        void Add(mqtt::MessageArrived&& message) {
            std::lock_guard<std::mutex> lock(mutex_);
            messages_.push_back(std::move(message));
            ready_.notify_all();
        }

        template <typename Predicate>
        std::optional<mqtt::MessageArrived> WaitFor(Predicate matches,
                                                    std::chrono::milliseconds timeout = 5s) {
            std::unique_lock<std::mutex> lock(mutex_);
            std::optional<mqtt::MessageArrived> result;
            ready_.wait_for(lock, timeout, [&]() {
                for (auto it = messages_.begin(); it != messages_.end(); ++it) {
                    if (matches(Json::parse(it->payload))) {
                        result = std::move(*it);
                        messages_.erase(it);
                        return true;
                    }
                }
                return false;
            });
            return result;
        }

        std::optional<mqtt::MessageArrived> WaitForId(const std::string& request_id,
                                                      std::chrono::milliseconds timeout = 5s) {
            return WaitFor(
                [&](const Json& message) { return message.at("head").at("requestId") == request_id; },
                timeout);
        }

    private:
        std::mutex mutex_;
        std::condition_variable ready_;
        std::vector<mqtt::MessageArrived> messages_;
    };

    struct BrokerClient {
        explicit BrokerClient(const BrokerAddress& address) {
            REQUIRE(client.MQTTClientSetCallbacks(
                        &inbox,
                        [](mqtt::MessageArrived&& message, void* context) {
                            static_cast<MessageInbox*>(context)->Add(std::move(message));
                        },
                        nullptr, nullptr) == 0);
            mqtt::MqttConnectOptions options;
            options.server_uri        = "tcp://" + address.host + ":" + std::to_string(address.port);
            options.client_id         = "cosmo-mqtt-test-" + cosmo::util::GenerateUUID();
            options.auth_type         = mqtt::MqttAuthType::kAnonymous;
            options.connect_timeout_s = 3;
            REQUIRE(client.MQTTClientCreate(options) == 0);
            REQUIRE(client.MQTTClientSubscribe(base_topic + "/reply", 1) == 0);
            REQUIRE(client.MQTTClientSubscribe(base_topic + "/observed", 1) == 0);
        }

        mqtt::MqttMessage Request(const std::string& scenario, const std::string& request_id) const {
            return {base_topic + "/request/" + scenario,
                    Json{{"head", {{"requestId", request_id}, {"msgType", "request"}}},
                         {"body", {{"value", request_id}}}}
                        .dump(),
                    1};
        }

        const std::string base_topic = "cosmo-mqtt-test/" + cosmo::util::GenerateUUID();
        MessageInbox inbox;
        mqtt::CMvMQTTClient client;
    };

    void CheckAck(const mqtt::SyncPubResult& result, const mqtt::MqttMessage& request,
                  const std::string& reply_topic) {
        const auto payload = Json::parse(result.payload);
        CHECK(result.request_id == Json::parse(request.payload).at("head").at("requestId"));
        CHECK(result.sync_state ==
              (static_cast<int>(mqtt::SyncPubState::kSent) | static_cast<int>(mqtt::SyncPubState::kAcked)));
        CHECK(result.topic == reply_topic);
        CHECK(result.qos == 1);
        CHECK(payload.at("body").at("receivedTopic") == request.topic);
        CHECK(payload.at("body").at("receivedPayload") == request.payload);
    }

    bool WaitForRegistered(service::MqttLifecycleServiceImpl& service) {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (std::chrono::steady_clock::now() < deadline) {
            if (service.IsMqttRegistered()) {
                return true;
            }
            std::this_thread::sleep_for(10ms);
        }
        return false;
    }

    class EchoDispatcher : public cosmo::IRequestDispatcher {
    public:
        bool SupportsRoute(const std::string& uri) override {
            return uri == "/mqtt-broker-test/echo";
        }

        cosmo::RequestAdmission InspectRequest(cosmo::RequestDispatchContext& context,
                                               bool /*require_known_route*/) override {
            return SupportsRoute(context.uri) ? cosmo::RequestAdmission::kAllowed
                                              : cosmo::RequestAdmission::kRouteNotFound;
        }

        bool DispatchRequest(const cosmo::RequestDispatchContext& context, const std::string& body,
                             std::string& response) override {
            if (!SupportsRoute(context.uri)) {
                return false;
            }
            response = body;
            return true;
        }
    };

}  // namespace

TEST_CASE("MQTT client: immediate business ACK and asynchronous reply", "[mqtt-broker][mqtt-client]") {
    BrokerClient fixture(ReadBrokerAddress());
    const auto request = fixture.Request("ack", cosmo::util::GenerateUUID());

    SECTION("One business ACK completes the synchronous publish") {
        mqtt::SyncPubResult result;
        REQUIRE(fixture.client.MQTTClientPublish(request, 2000, &result) == 0);
        CheckAck(result, request, fixture.base_topic + "/reply");
        CHECK_FALSE(fixture.inbox.WaitForId(result.request_id, 100ms).has_value());
    }

    SECTION("The default publish leaves the reply for the ordinary callback") {
        REQUIRE(fixture.client.MQTTClientPublish(request) == 0);
        const auto reply = fixture.inbox.WaitForId(Json::parse(request.payload).at("head").at("requestId"));
        REQUIRE(reply.has_value());
        CHECK(reply->topic == fixture.base_topic + "/reply");
        CHECK(Json::parse(reply->payload).at("body").at("receivedPayload") == request.payload);
    }
}

TEST_CASE("MQTT client: QoS PUBACK does not satisfy the business ACK wait", "[mqtt-broker][mqtt-client]") {
    BrokerClient fixture(ReadBrokerAddress());
    const auto request = fixture.Request("timeout", cosmo::util::GenerateUUID());
    mqtt::SyncPubResult result;
    const auto started = std::chrono::steady_clock::now();

    CHECK(fixture.client.MQTTClientPublish(request, 200, &result) ==
          static_cast<int>(mqtt::MqttErrorCode::kWaitAckTimeout));
    CHECK(std::chrono::steady_clock::now() - started >= 200ms);
    CHECK(result.sync_state == static_cast<int>(mqtt::SyncPubState::kSent));
    const auto receipt = fixture.inbox.WaitForId("receipt/" + result.request_id);
    REQUIRE(receipt.has_value());
    CHECK(Json::parse(receipt->payload).at("body").at("receivedPayload") == request.payload);
    CHECK(Json::parse(receipt->payload).at("body").at("receivedQos") == 1);
}

TEST_CASE("MQTT client: late ACK is delivered normally after timeout cleanup", "[mqtt-broker][mqtt-client]") {
    BrokerClient fixture(ReadBrokerAddress());
    const auto request_id = cosmo::util::GenerateUUID();
    mqtt::SyncPubResult result;
    REQUIRE(fixture.client.MQTTClientPublish(fixture.Request("late", request_id), 100, &result) ==
            static_cast<int>(mqtt::MqttErrorCode::kWaitAckTimeout));
    const auto late_reply = fixture.inbox.WaitForId(request_id);
    REQUIRE(late_reply.has_value());
    CHECK(late_reply->topic == fixture.base_topic + "/reply");

    // Reusing the ID after the late callback also proves the timed-out entry was removed.
    const auto retry = fixture.Request("ack", request_id);
    REQUIRE(fixture.client.MQTTClientPublish(retry, 2000, &result) == 0);
    CheckAck(result, retry, fixture.base_topic + "/reply");
}

TEST_CASE("MQTT client: an unrelated ID does not complete a pending publish", "[mqtt-broker][mqtt-client]") {
    BrokerClient fixture(ReadBrokerAddress());
    const auto request_id = cosmo::util::GenerateUUID();
    mqtt::SyncPubResult result;
    REQUIRE(fixture.client.MQTTClientPublish(fixture.Request("mismatch", request_id), 200, &result) ==
            static_cast<int>(mqtt::MqttErrorCode::kWaitAckTimeout));
    CHECK(result.request_id == request_id);
    CHECK(result.sync_state == static_cast<int>(mqtt::SyncPubState::kSent));
    REQUIRE(fixture.inbox.WaitForId("unmatched/" + request_id).has_value());
}

TEST_CASE("MQTT client: concurrent replies correlate in reverse arrival order",
          "[mqtt-broker][mqtt-client]") {
    BrokerClient fixture(ReadBrokerAddress());
    struct PublishResult {
        mqtt::MqttMessage request;
        mqtt::SyncPubResult reply;
        int status;
    };
    std::vector<std::future<PublishResult>> pending;
    for (int index = 0; index < 4; ++index) {
        pending.push_back(std::async(std::launch::async, [&]() {
            PublishResult result;
            result.request = fixture.Request("reverse", cosmo::util::GenerateUUID());
            result.status  = fixture.client.MQTTClientPublish(result.request, 5000, &result.reply);
            return result;
        }));
    }
    for (auto& publish : pending) {
        const auto result = publish.get();
        REQUIRE(result.status == 0);
        CheckAck(result.reply, result.request, fixture.base_topic + "/reply");
        const auto body = Json::parse(result.reply.payload).at("body");
        CHECK(body.at("receiveOrder").get<int>() + body.at("replyOrder").get<int>() == 3);
    }
}

TEST_CASE("MQTT lifecycle: connection loss registers again and restores both subscriptions",
          "[mqtt-broker][mqtt-lifecycle]") {
    const auto address = ReadBrokerAddress();
    BrokerClient controller(address);
    REQUIRE(controller.client.MQTTClientSubscribe("/d2p/aibox", 1) == 0);
    const std::string device_sn = "cosmo-mqtt-test-" + cosmo::util::GenerateUUID();
    MockConfigReadService config_read;
    MockConfigNetworkService config_network;
    MockDeviceInfoService device;
    MockAppInfoService app;
    ScopedServiceOverride<service::IConfigReadService> config_read_guard(config_read);
    ScopedServiceOverride<service::IConfigNetworkService> config_network_guard(config_network);
    ScopedServiceOverride<service::IDeviceHardware> device_guard(device);
    ScopedServiceOverride<service::IAppInfoService> app_guard(app);
    service::MqttParam parameters;
    parameters.url  = address.host;
    parameters.port = address.port;
    ALLOW_CALL(config_read, GetRunMode()).RETURN(cosmo::RunMode::RunModeStandAlone);
    ALLOW_CALL(config_network, GetMqttParam()).RETURN(parameters);
    ALLOW_CALL(device, GetDevSn()).RETURN(device_sn);
    ALLOW_CALL(device, GetDevModel()).RETURN("mqtt-broker-test");
    ALLOW_CALL(app, GetEngineType()).RETURN("mqtt-broker-test");
    ALLOW_CALL(app, GetSystemOverviewInfo(trompeloeil::_, trompeloeil::_)).RETURN(cosmo::MsgInfoSend{});
    service::MqttLifecycleServiceImpl lifecycle([]() { return std::make_unique<EchoDispatcher>(); });
    lifecycle.MqttStart();

    const auto registration = [&](const Json& message) {
        const auto& head = message.at("head");
        return head.at("deviceSn") == device_sn && head.at("msgType") == "register";
    };
    const auto probe_subscriptions = [&](const std::string& phase) {
        for (const std::string& suffix : {std::string(), std::string("heartbeat/")}) {
            mqtt::MqttMessage request;
            request.topic   = "/p2d/aibox/" + suffix + device_sn;
            request.qos     = 1;
            request.payload = Json{
                {"head",
                 {{"requestId", cosmo::util::GenerateUUID()},
                  {"deviceSn", device_sn},
                  {"msgType", "request"},
                  {"action", "/mqtt-broker-test/echo"}}},
                {"body",
                 phase + suffix}}.dump();
            mqtt::SyncPubResult result;
            REQUIRE(controller.client.MQTTClientPublish(request, 3000, &result) == 0);
            CHECK(result.topic == "/d2p/aibox");
            const auto reply = Json::parse(result.payload);
            CHECK(reply.at("head").at("msgType") == "response");
            CHECK(reply.at("body") == phase + suffix);
        }
    };

    REQUIRE(controller.inbox.WaitFor(registration).has_value());
    REQUIRE(WaitForRegistered(lifecycle));
    probe_subscriptions("before-disconnect");

    auto disconnect                        = controller.Request("disconnect", cosmo::util::GenerateUUID());
    auto disconnect_payload                = Json::parse(disconnect.payload);
    disconnect_payload["body"]["clientId"] = device_sn;
    disconnect.payload                     = disconnect_payload.dump();
    mqtt::SyncPubResult disconnect_result;
    REQUIRE(controller.client.MQTTClientPublish(disconnect, 2000, &disconnect_result) == 0);
    REQUIRE(Json::parse(disconnect_result.payload).at("body").at("disconnected") == true);
    REQUIRE(controller.inbox.WaitFor(registration, 15s).has_value());
    REQUIRE(WaitForRegistered(lifecycle));
    probe_subscriptions("after-disconnect");

    lifecycle.MqttStop();
    CHECK_FALSE(lifecycle.IsMqttRegistered());
    CHECK_FALSE(lifecycle.IsMqttEnabled());
}

}  // namespace cosmo::test
