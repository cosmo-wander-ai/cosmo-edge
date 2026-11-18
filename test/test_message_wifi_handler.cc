// Wi-Fi dispatch keeps transport provenance and write-only credentials out of errors.
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

#include "api/MessageWifiHandler.h"
#include "catch_amalgamated.hpp"
#include "util/ErrorCode.h"
#include "util/MsgBaseTypes.h"

namespace cosmo::test {
namespace {
    class FakeWifiConfig final : public service::IWifiConfig {
    public:
        void Init() override {}
        void Stop() override {}
        nlohmann::json QueryStatus() override {
            ++calls;
            return {{"available", true}, {"state", "disconnected"}};
        }
        nlohmann::json Scan() override {
            ++calls;
            return {{"operationId", "scan-pending"}};
        }
        nlohmann::json Connect(const nlohmann::json& config) override {
            ++calls;
            last_config = config;
            return {{"operationId", "connect-pending"}};
        }
        nlohmann::json QueryOperation(const std::string& operation_id) override {
            ++calls;
            last_operation_id = operation_id;
            return {{"operationId", operation_id}, {"stage", "connecting"}, {"done", false}};
        }
        nlohmann::json Confirm(const std::string& operation_id) override {
            ++calls;
            last_operation_id = operation_id;
            return {{"operationId", operation_id}};
        }
        nlohmann::json Disconnect() override {
            ++calls;
            return {{"operationId", "disconnect-pending"}};
        }
        nlohmann::json Forget() override {
            ++calls;
            return {{"operationId", "forget-pending"}};
        }
        bool Reset() override {
            throw std::logic_error("Unexpected factory reset from Wi-Fi handler");
        }

        int calls{0};
        nlohmann::json last_config;
        std::string last_operation_id;
    };

    RequestDispatchContext HttpPostContext() {
        RequestDispatchContext context;
        context.transport   = RequestTransport::kHttp;
        context.http_method = "POST";
        return context;
    }
}  // namespace

TEST_CASE("MessageWifiHandler: transport comes from the request context", "[wifi-handler]") {
    FakeWifiConfig wifi;
    MessageWifiHandler handler(wifi);
    auto context = HttpPostContext();
    SECTION("MQTT cannot claim HTTP provenance in the body") {
        context.transport = RequestTransport::kMqtt;
    }
    SECTION("HTTP GET cannot invoke a Wi-Fi operation") {
        context.http_method = "GET";
    }
    SECTION("An absent HTTP method cannot invoke a Wi-Fi operation") {
        context.http_method.clear();
    }
    std::error_condition error;
    const auto response = nlohmann::json::parse(handler.Handle(
        "connectWifi", context, R"({"messageFrom":0,"transport":"http","http_method":"POST"})", error));
    CHECK(response.at("resCode") == kServerRspFailed);
    CHECK(error == util::ErrorEnum::Failed);
    CHECK(wifi.calls == 0);
    CHECK_FALSE(response.contains("resData"));
}

TEST_CASE("MessageWifiHandler: accepted requests return pending operation IDs", "[wifi-handler]") {
    FakeWifiConfig wifi;
    MessageWifiHandler handler(wifi);
    const auto context = HttpPostContext();
    std::error_condition error;

    SECTION("Scan acceptance does not claim connection success") {
        const auto response = nlohmann::json::parse(handler.Handle("scanWifi", context, "{}", error));
        REQUIRE(response.at("resCode") == kServerRspSuccess);
        CHECK(response.at("resData").at("operationId") == "scan-pending");
        CHECK_FALSE(response.at("resData").contains("success"));
        CHECK_FALSE(response.at("resData").contains("state"));
    }
    SECTION("Connect preserves SSID bytes and keeps credentials write-only") {
        const nlohmann::json request{
            {"ssid", "Office wifi 中文 !"}, {"password", "test-password-only"}, {"messageFrom", "mqtt"}};
        const auto body     = handler.Handle("connectWifi", context, request.dump(), error);
        const auto response = nlohmann::json::parse(body);
        REQUIRE(response.at("resCode") == kServerRspSuccess);
        CHECK(response.at("resData").at("operationId") == "connect-pending");
        CHECK(wifi.last_config.at("ssid") == request.at("ssid"));
        CHECK(wifi.last_config.at("password") == request.at("password"));
        CHECK(body.find("test-password-only") == std::string::npos);
    }
    SECTION("Operation query returns the actual pending stage") {
        const auto response = nlohmann::json::parse(
            handler.Handle("queryWifiOperation", context, R"({"operationId":"connect-pending"})", error));
        CHECK(response.at("resData").at("done") == false);
        CHECK(response.at("resData").at("stage") == "connecting");
        CHECK(wifi.last_operation_id == "connect-pending");
    }
    SECTION("Confirmation forwards its operation ID") {
        const auto response = nlohmann::json::parse(
            handler.Handle("confirmWifiConfig", context, R"({"operationId":"connect-pending"})", error));
        CHECK(response.at("resCode") == kServerRspSuccess);
        CHECK(wifi.last_operation_id == "connect-pending");
    }
    CHECK(error == util::ErrorEnum::Success);
    CHECK(wifi.calls == 1);
}

TEST_CASE("MessageWifiHandler: parse and schema errors never echo credentials", "[wifi-handler]") {
    FakeWifiConfig wifi;
    MessageWifiHandler handler(wifi);
    const auto context = HttpPostContext();
    std::string action = "connectWifi";
    std::string body;
    SECTION("Malformed JSON after a password") {
        body = R"({"password":"write-only-secret" invalid})";
    }
    SECTION("A request must be an object") {
        body = R"(["write-only-secret"])";
    }
    SECTION("Operation queries require an ID") {
        action = "queryWifiOperation";
        body   = R"({"password":"write-only-secret"})";
    }
    SECTION("Confirmations require an ID") {
        action = "confirmWifiConfig";
        body   = R"({"password":"write-only-secret"})";
    }
    SECTION("Operation IDs must be strings") {
        action = "queryWifiOperation";
        body   = R"({"operationId":{"password":"write-only-secret"}})";
    }
    SECTION("Confirmation IDs must be strings") {
        action = "confirmWifiConfig";
        body   = R"({"operationId":42,"password":"write-only-secret"})";
    }
    std::error_condition error;
    const auto response_body = handler.Handle(action, context, body, error);
    const auto response      = nlohmann::json::parse(response_body);
    CHECK(response.at("resCode") == kServerRspFailed);
    CHECK(error != util::ErrorEnum::Success);
    CHECK(response_body.find("write-only-secret") == std::string::npos);
    CHECK_FALSE(response.contains("resData"));
    CHECK(wifi.calls == 0);
}
}  // namespace cosmo::test
