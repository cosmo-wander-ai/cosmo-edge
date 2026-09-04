#include "catch_amalgamated.hpp"
/*
 * test_mqtt_lifecycle_service_impl.cc — MqttLifecycleServiceImpl unit tests
 *
 * Strategy: Test construction/destruction and state queries.
 * Actual MQTT connection requires a broker, so connection tests are minimal.
 */
#include <arpa/inet.h>
#include <event2/dns.h>
#include <event2/event.h>
#include <event2/util.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "mock/MockConfigNetworkService.h"
#include "mock/MockConfigReadService.h"
#include "mock/MockDeviceInfoService.h"
#include "service/network/impl/MqttLifecycleServiceImpl.h"
#include "support/ScopedServiceOverride.h"
#include "util/IRequestDispatcher.h"

using namespace cosmo::service;

namespace {

struct MqttDependencies {
    cosmo::test::MockConfigReadService configReadSvc;
    cosmo::test::MockConfigNetworkService configNetSvc;
    cosmo::test::MockDeviceInfoService deviceInfoSvc;
    cosmo::test::ScopedServiceOverride<IConfigReadService> configRead{configReadSvc};
    cosmo::test::ScopedServiceOverride<IConfigNetworkService> configNetwork{configNetSvc};
    cosmo::test::ScopedServiceOverride<IDeviceInfoService> deviceInfo{deviceInfoSvc};
};

class StubDispatcher : public cosmo::IRequestDispatcher {
public:
    bool SupportsRoute(const std::string& /*uri*/) override {
        return false;
    }
    cosmo::RequestAdmission InspectRequest(cosmo::RequestDispatchContext& /*context*/,
                                           bool /*require_known_route*/) override {
        return cosmo::RequestAdmission::kRouteNotFound;
    }
    bool DispatchRequest(const cosmo::RequestDispatchContext& /*context*/, const std::string& /*body*/,
                         std::string& /*response*/) override {
        return false;
    }
};

struct EventBaseDeleter {
    void operator()(event_base* base) const {
        if (base != nullptr) {
            event_base_free(base);
        }
    }
};

struct DnsBaseDeleter {
    void operator()(evdns_base* base) const {
        if (base != nullptr) {
            evdns_base_free(base, 0);
        }
    }
};

struct DnsTestResult {
    ~DnsTestResult() {
        if (addresses != nullptr) {
            evutil_freeaddrinfo(addresses);
        }
    }

    bool completed{false};
    int error{EVUTIL_EAI_FAIL};
    evutil_addrinfo* addresses{nullptr};
};

class LoopbackResetServer {
public:
    LoopbackResetServer() = default;

    ~LoopbackResetServer() {
        CloseListener();
    }

    LoopbackResetServer(const LoopbackResetServer&)            = delete;
    LoopbackResetServer& operator=(const LoopbackResetServer&) = delete;

    bool Start() {
        listener_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listener_ < 0) {
            return false;
        }

        int reuse_address = 1;
        if (setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &reuse_address, sizeof(reuse_address)) != 0) {
            return false;
        }

        sockaddr_in address{};
        address.sin_family      = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port        = 0;
        if (bind(listener_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
            listen(listener_, 1) != 0) {
            return false;
        }

        socklen_t address_length = sizeof(address);
        if (getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &address_length) != 0) {
            return false;
        }
        port_ = ntohs(address.sin_port);
        return true;
    }

    int Port() const {
        return port_;
    }

    bool AcceptAndReset(std::chrono::milliseconds timeout) {
        pollfd descriptor{listener_, POLLIN, 0};
        if (poll(&descriptor, 1, static_cast<int>(timeout.count())) != 1 ||
            (descriptor.revents & POLLIN) == 0) {
            return false;
        }

        const int connection = accept(listener_, nullptr, nullptr);
        if (connection < 0) {
            return false;
        }
        CloseListener();

        linger reset{1, 0};
        const bool configured = setsockopt(connection, SOL_SOCKET, SO_LINGER, &reset, sizeof(reset)) == 0;
        close(connection);
        return configured;
    }

private:
    void CloseListener() {
        if (listener_ >= 0) {
            close(listener_);
            listener_ = -1;
        }
    }

    int listener_{-1};
    int port_{0};
};

}  // namespace

TEST_CASE("MqttLifecycleServiceImpl: cancelled DNS callback is drained before teardown",
          "[mqtt-lifecycle][dns][lifecycle]") {
    std::unique_ptr<event_base, EventBaseDeleter> event_base_ptr(event_base_new());
    REQUIRE(event_base_ptr != nullptr);
    std::unique_ptr<evdns_base, DnsBaseDeleter> dns_base_ptr(evdns_base_new(event_base_ptr.get(), 0));
    REQUIRE(dns_base_ptr != nullptr);
    REQUIRE(evdns_base_nameserver_ip_add(dns_base_ptr.get(), "127.0.0.1:65535") == 0);

    evutil_addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    DnsTestResult result;
    const auto callback = [](int error, evutil_addrinfo* addresses, void* context) {
        auto* resolution      = static_cast<DnsTestResult*>(context);
        resolution->completed = true;
        resolution->error     = error;
        resolution->addresses = addresses;
    };
    auto* request = evdns_getaddrinfo(dns_base_ptr.get(), "pending-dns-request.example", nullptr, &hints,
                                      callback, &result);
    REQUIRE(request != nullptr);

    CHECK(cosmo::service::detail::CancelAndDrainDnsRequest(event_base_ptr.get(), request, &result.completed));
    CHECK(result.completed);
    CHECK(result.error == EVUTIL_EAI_CANCEL);
    CHECK(result.addresses == nullptr);
}

TEST_CASE("MqttLifecycleServiceImpl: construction and destruction", "[mqtt-lifecycle]") {
    REQUIRE_NOTHROW(
        []() { MqttLifecycleServiceImpl sut([]() { return std::make_unique<StubDispatcher>(); }); }());
}

TEST_CASE("MqttLifecycleServiceImpl: initial state", "[mqtt-lifecycle]") {
    MqttLifecycleServiceImpl sut([]() { return std::make_unique<StubDispatcher>(); });

    SECTION("IsMqttRegistered returns false initially") {
        REQUIRE(sut.IsMqttRegistered() == false);
    }

    SECTION("IsMqttEnabled returns false initially") {
        REQUIRE(sut.IsMqttEnabled() == false);
    }
}

TEST_CASE("MqttLifecycleServiceImpl: MqttStop without MqttStart is safe", "[mqtt-lifecycle]") {
    MqttLifecycleServiceImpl sut([]() { return std::make_unique<StubDispatcher>(); });
    REQUIRE_NOTHROW(sut.MqttStop());
}

TEST_CASE("MqttLifecycleServiceImpl: double MqttStop is safe", "[mqtt-lifecycle]") {
    MqttLifecycleServiceImpl sut([]() { return std::make_unique<StubDispatcher>(); });
    REQUIRE_NOTHROW(sut.MqttStop());
    REQUIRE_NOTHROW(sut.MqttStop());
}

TEST_CASE("MqttLifecycleServiceImpl: permanent shutdown is idempotent and not restartable",
          "[mqtt-lifecycle][lifecycle]") {
    MqttLifecycleServiceImpl sut([]() { return std::make_unique<StubDispatcher>(); });
    REQUIRE_NOTHROW(sut.MqttShutdown());
    REQUIRE_NOTHROW(sut.MqttShutdown());

    // Restart must return before consulting ServiceRegistry configuration.
    REQUIRE_NOTHROW(sut.MqttStart());
    REQUIRE_NOTHROW(sut.MqttStop());
    REQUIRE_FALSE(sut.IsMqttEnabled());
    REQUIRE_FALSE(sut.IsMqttRegistered());
}

TEST_CASE("MqttLifecycleServiceImpl: concurrent permanent shutdown is serialized",
          "[mqtt-lifecycle][lifecycle][concurrency]") {
    MqttLifecycleServiceImpl sut([]() { return std::make_unique<StubDispatcher>(); });
    std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i) {
        workers.emplace_back([&sut]() { sut.MqttShutdown(); });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    REQUIRE_FALSE(sut.IsMqttEnabled());
    REQUIRE_FALSE(sut.IsMqttRegistered());
}

TEST_CASE("MqttLifecycleServiceImpl: MqttStart and MqttStop lifecycle", "[mqtt-lifecycle]") {
    MqttDependencies mocks;

    ALLOW_CALL(mocks.configReadSvc, GetRunMode()).RETURN(cosmo::RunMode::RunModeStandAlone);
    cosmo::service::MqttParam mqttP;
    mqttP.enable = false;  // Disabled — no actual connection
    ALLOW_CALL(mocks.configNetSvc, GetMqttParam()).RETURN(mqttP);
    ALLOW_CALL(mocks.deviceInfoSvc, GetDevSn()).RETURN("SN-TEST");

    MqttLifecycleServiceImpl sut([]() { return std::make_unique<StubDispatcher>(); });
    REQUIRE_NOTHROW(sut.MqttStart());
    REQUIRE_NOTHROW(sut.MqttStop());
}

TEST_CASE("MqttLifecycleServiceImpl: concurrent disabled start and stop is serialized",
          "[mqtt-lifecycle][concurrency]") {
    MqttDependencies mocks;

    ALLOW_CALL(mocks.configReadSvc, GetRunMode()).RETURN(cosmo::RunMode::RunModeStandAlone);
    cosmo::service::MqttParam mqtt_param;
    mqtt_param.enable = false;
    ALLOW_CALL(mocks.configNetSvc, GetMqttParam()).RETURN(mqtt_param);

    MqttLifecycleServiceImpl sut([]() { return std::make_unique<StubDispatcher>(); });
    std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i) {
        workers.emplace_back([&sut, i]() {
            for (int attempt = 0; attempt < 10; ++attempt) {
                if ((attempt + i) % 2 == 0) {
                    sut.MqttStart();
                } else {
                    sut.MqttStop();
                }
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    sut.MqttStop();
    REQUIRE_FALSE(sut.IsMqttEnabled());
    REQUIRE_FALSE(sut.IsMqttRegistered());
}

TEST_CASE("MqttLifecycleServiceImpl: Stop interrupts reconnect backoff promptly",
          "[mqtt-lifecycle][lifecycle]") {
    MqttDependencies mocks;

    LoopbackResetServer server;
    REQUIRE(server.Start());

    ALLOW_CALL(mocks.configReadSvc, GetRunMode()).RETURN(cosmo::RunMode::RunModeStandAlone);
    cosmo::service::MqttParam mqtt_param;
    mqtt_param.enable = true;
    mqtt_param.url    = "127.0.0.1";
    mqtt_param.port   = server.Port();
    ALLOW_CALL(mocks.configNetSvc, GetMqttParam()).RETURN(mqtt_param);
    ALLOW_CALL(mocks.deviceInfoSvc, GetDevSn()).RETURN("SN-MQTT-STOP-TEST");

    MqttLifecycleServiceImpl sut([]() { return std::make_unique<StubDispatcher>(); });
    sut.MqttStart();
    REQUIRE(server.AcceptAndReset(std::chrono::seconds(2)));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    const auto stop_started = std::chrono::steady_clock::now();
    sut.MqttStop();
    const auto stop_elapsed = std::chrono::steady_clock::now() - stop_started;

    CHECK(stop_elapsed < std::chrono::seconds(1));
    CHECK_FALSE(sut.IsMqttEnabled());
    CHECK_FALSE(sut.IsMqttRegistered());
}
