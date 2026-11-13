#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>

#include "catch_amalgamated.hpp"
#include "platform/WifiPlatform.h"
#include "service/network/NetworkMutation.h"
#include "service/network/impl/WifiConfigServiceImpl.h"
#include "support/ScopedPathOverride.h"

namespace {
using Json = nlohmann::json;
using namespace std::chrono_literals;
using cosmo::platform::NetworkInterfaceState;
using cosmo::service::WifiConfigServiceImpl;
struct FakeWifi final : cosmo::platform::IWifiPlatform {
    std::mutex mutex;
    bool present{true};
    bool connected{false};
    bool address_available{true};
    bool fail_auth{false};
    bool fail_apply{false};
    bool fail_restore{false};
    bool conflict{false};
    bool emit_scan{true};
    int prepare_count{0};
    int restore_count{0};
    int apply_count{0};
    int clear_count{0};
    int commit_count{0};
    std::string actual_ip;
    std::string actual_mask{"255.255.255.0"};
    std::string ssid;
    std::string reported_ssid;
    std::deque<std::string> events;
    std::vector<NetworkInterfaceState> Interfaces() override {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<NetworkInterfaceState> result{
            {"eth0", "ethernet", true, "192.0.2.10", "255.255.255.0", "192.0.2.1", true, 100}};
        if (present)
            result.push_back({"wlP1p1s0", "wifi", connected, connected && address_available ? actual_ip : "",
                              actual_mask, "192.0.2.1", false, 600});
        return result;
    }
    bool IsSupported() const override {
        return true;
    }
    Json Snapshot(const std::string&) override {
        std::lock_guard<std::mutex> lock(mutex);
        return {{"connected", connected}, {"ip", actual_ip}, {"ssid", ssid}};
    }
    void Prepare(const std::string&) override {
        ++prepare_count;
    }
    std::string Request(const std::string&, const std::string& command) override {
        std::lock_guard<std::mutex> lock(mutex);
        if (command == "STATUS")
            return "wpa_state=" + std::string(connected ? "COMPLETED" : "DISCONNECTED") +
                   "\nssid=" + (reported_ssid.empty() ? ssid : reported_ssid) + "\n";
        if (command == "SCAN") {
            if (emit_scan)
                events.push_back("<3>CTRL-EVENT-SCAN-RESULTS");
            return "OK\n";
        }
        if (command == "SCAN_RESULTS")
            return "bssid / frequency / signal level / flags / ssid\n"
                   "00:11:22:33:44:55\t2412\t-42\t[WPA2-PSK-CCMP][ESS]\ttest AP\n";
        return "OK\n";
    }
    std::string WaitEvent(const std::string&, std::chrono::milliseconds timeout) override {
        if (timeout.count())
            std::this_thread::sleep_for(1ms);
        std::lock_guard<std::mutex> lock(mutex);
        if (events.empty())
            return {};
        auto event = events.front();
        events.pop_front();
        return event;
    }
    void Apply(const std::string&, const Json& settings) override {
        std::lock_guard<std::mutex> lock(mutex);
        ++apply_count;
        if (fail_apply)
            throw std::runtime_error("addressConfigFailed");
        connected = !fail_auth;
        actual_ip =
            settings.at("dhcp").get<bool>() ? "192.0.2.123" : settings.at("ipAddr").get<std::string>();
        actual_mask =
            settings.at("dhcp").get<bool>() ? "255.255.255.0" : settings.at("netMask").get<std::string>();
        ssid = settings.at("ssid");
        if (fail_auth)
            events.push_back("<3>CTRL-EVENT-SSID-TEMP-DISABLED reason=WRONG_KEY");
    }
    void Restore(const std::string&, const Json& snapshot) override {
        std::lock_guard<std::mutex> lock(mutex);
        ++restore_count;
        if (fail_restore)
            throw std::runtime_error("rollbackFailed");
        connected = snapshot.at("connected");
        actual_ip = snapshot.at("ip");
        ssid      = snapshot.at("ssid");
    }
    void Commit(const std::string&, const Json&, bool) override {
        ++commit_count;
    }
    void Clear(const std::string&, bool) override {
        std::lock_guard<std::mutex> lock(mutex);
        ++clear_count;
        connected = false;
        actual_ip.clear();
    }
    bool HasAddressConflict(const std::string&, const std::string&) override {
        return conflict;
    }
    void UpdateDnsRoute(const std::string&) override {}
};
struct Fixture {
    std::string root;
    cosmo::service::WifiServiceOptions options;
    std::shared_ptr<FakeWifi> platform = std::make_shared<FakeWifi>();
    Fixture() {
        static std::atomic<unsigned> sequence{0};
        root = (std::filesystem::temp_directory_path() /
                ("cosmo-wifi-test-" + std::to_string(getpid()) + "-" + std::to_string(sequence.fetch_add(1))))
                   .string();
        options.private_directory = root + "/private";
        options.metadata_file     = root + "/app/wifi.json";
        options.poll_interval     = 1ms;
        options.scan_timeout      = 100ms;
        options.connect_timeout   = 100ms;
        options.confirm_timeout   = 100ms;
    }
    ~Fixture() {
        std::filesystem::remove_all(root);
    }
    Json Config() const {
        return {{"ssid", "test AP"}, {"password", "wifi-test-password"}};
    }
};
Json WaitFor(WifiConfigServiceImpl& service, const std::string& id, const std::string& stage = "") {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    Json operation;
    while (std::chrono::steady_clock::now() < deadline) {
        operation = service.QueryOperation(id);
        if ((stage.empty() && operation.value("done", false)) ||
            (!stage.empty() && operation.value("stage", "") == stage))
            return operation;
        std::this_thread::sleep_for(1ms);
    }
    FAIL("Wi-Fi operation did not reach expected state");
    return operation;
}
void WaitIdle() {
    for (int i = 0; i < 500 && cosmo::service::NetworkMutationLease::IsBusy(); ++i)
        std::this_thread::sleep_for(1ms);
    REQUIRE_FALSE(cosmo::service::NetworkMutationLease::IsBusy());
}
std::string ConfirmConnection(WifiConfigServiceImpl& service, const Json& config) {
    const auto id = service.Connect(config).at("operationId").get<std::string>();
    REQUIRE(WaitFor(service, id, "awaitingConfirmation").at("success") == false);
    service.Confirm(id);
    REQUIRE(WaitFor(service, id).at("success") == true);
    WaitIdle();
    return id;
}
}  // namespace

TEST_CASE("Wi-Fi validates byte-length SSIDs and protects control syntax", "[wifi]") {
    Fixture fixture;
    auto input            = fixture.Config();
    input["ssid"]         = "中文 AP \";$(echo injected)";
    const auto config     = cosmo::platform::ValidateWifiSettings(input, Json::object());
    const auto supplicant = cosmo::platform::WifiSupplicantConfig(config, true);
    REQUIRE(supplicant.find("echo injected") == std::string::npos);
    REQUIRE(supplicant.find("wifi-test-password") == std::string::npos);
    REQUIRE(config.at("psk").get<std::string>().size() == 64);
    input["ssid"] = std::string(33, 'a');
    REQUIRE_THROWS_WITH(cosmo::platform::ValidateWifiSettings(input, Json::object()), "invalidSsid");
    input["ssid"]     = "valid";
    input["password"] = "short";
    REQUIRE_THROWS_WITH(cosmo::platform::ValidateWifiSettings(input, Json::object()), "invalidPassword");
}
TEST_CASE("Wi-Fi PBKDF2 matches the WPA reference vector", "[wifi]") {
    const auto settings =
        cosmo::platform::ValidateWifiSettings({{"ssid", "IEEE"}, {"password", "password"}}, Json::object());
    REQUIRE(settings.at("psk") == "f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e");
}
TEST_CASE("Wi-Fi rejects invalid static address DNS and unsupported security", "[wifi]") {
    Fixture fixture;
    auto config       = fixture.Config();
    config["dhcp"]    = false;
    config["autoDns"] = false;
    config["dns"]     = Json::array({"192.0.2.1"});
    config["ipAddr"]  = "192.0.2.0";
    config["netMask"] = "255.255.255.0";
    REQUIRE_THROWS_WITH(cosmo::platform::ValidateWifiSettings(config, {}), "invalidIpv4");
    config["ipAddr"]  = "192.0.2.12";
    config["netMask"] = "255.0.255.0";
    REQUIRE_THROWS_WITH(cosmo::platform::ValidateWifiSettings(config, {}), "invalidNetMask");
    config["netMask"] = "255.255.255.0";
    config["gateway"] = "198.51.100.1";
    REQUIRE_THROWS_WITH(cosmo::platform::ValidateWifiSettings(config, {}), "invalidGateway");
    config["gateway"] = "192.0.2.1";
    config["autoDns"] = true;
    REQUIRE_THROWS_WITH(cosmo::platform::ValidateWifiSettings(config, {}), "invalidDns");
    config["autoDns"] = false;
    config["dns"]     = Json::array();
    REQUIRE_THROWS_WITH(cosmo::platform::ValidateWifiSettings(config, {}), "invalidDns");
    config["dns"] = Json::array({"192.0.2.53"});
    REQUIRE_NOTHROW(cosmo::platform::ValidateWifiSettings(config, {}));
    config["security"] = "wpa3";
    REQUIRE_THROWS_WITH(cosmo::platform::ValidateWifiSettings(config, {}), "unsupportedSecurity");
}
TEST_CASE("Wi-Fi scan separates identical SSIDs with different security", "[wifi]") {
    const auto scan = cosmo::platform::ParseWifiScanResults(
        "bssid / frequency / signal level / flags / ssid\n"
        "00:00:00:00:00:01\t2412\t-70\t[WPA2-PSK-CCMP][ESS]\tsame AP\n"
        "00:00:00:00:00:02\t5180\t-40\t[WPA2-PSK+SAE-CCMP][ESS]\tsame AP\n"
        "00:00:00:00:00:03\t2412\t-60\t[ESS]\tsame AP\n"
        "00:00:00:00:00:04\t2412\t-60\t[WPA2-PSK-CCMP][ESS]\t\\xe4\\xb8\\xad\\xe6\\x96\\x87 AP\n");
    REQUIRE(scan.size() == 3);
    REQUIRE(scan[0].at("frequency") == 5180);
    REQUIRE(scan[0].at("security") == "wpa2");
    REQUIRE(scan[1].at("security") == "open");
    REQUIRE(scan[2].at("ssid") == "中文 AP");
}
TEST_CASE("Wi-Fi networkd config matches one interface and sets backup route metric", "[wifi]") {
    Fixture fixture;
    auto input        = fixture.Config();
    input["dhcp"]     = false;
    input["hidden"]   = true;
    input["ipAddr"]   = "192.0.2.20";
    input["netMask"]  = "255.255.255.0";
    input["gateway"]  = "192.0.2.1";
    input["autoDns"]  = false;
    input["dns"]      = Json::array({"192.0.2.53"});
    const auto config = cosmo::platform::ValidateWifiSettings(input, {});
    const auto file   = cosmo::platform::WifiNetworkConfig("wlP1p1s0", config);
    REQUIRE(file.find("Name=wlP1p1s0\n") != std::string::npos);
    REQUIRE(file.find("Address=192.0.2.20/24") != std::string::npos);
    REQUIRE(file.find("Metric=600") != std::string::npos);
    REQUIRE(file.find("UseDNS=no") != std::string::npos);
    REQUIRE(file.find("DNS=192.0.2.53") != std::string::npos);
    REQUIRE(cosmo::platform::WifiSupplicantConfig(config, true).find("scan_ssid=1") != std::string::npos);
}
TEST_CASE("Devices without Wi-Fi do not create config or start services", "[wifi]") {
    Fixture fixture;
    fixture.platform->present = false;
    WifiConfigServiceImpl service(fixture.platform, fixture.options);
    service.Init();
    REQUIRE(service.QueryStatus().at("available") == false);
    REQUIRE(fixture.platform->prepare_count == 0);
    REQUIRE_FALSE(std::filesystem::exists(fixture.options.private_directory));
    REQUIRE_THROWS_WITH(service.Scan(), "wifiUnavailable");
}
TEST_CASE("Wi-Fi scanning is asynchronous and does not leak credentials", "[wifi]") {
    Fixture fixture;
    WifiConfigServiceImpl service(fixture.platform, fixture.options);
    service.Init();
    const auto id     = service.Scan().at("operationId").get<std::string>();
    const auto result = WaitFor(service, id);
    REQUIRE(result.at("success") == true);
    REQUIRE(result.at("scanResults").size() == 1);
    REQUIRE(result.dump().find("password") == std::string::npos);
    WaitIdle();
}
TEST_CASE("Wi-Fi confirmation persists only safe metadata and shares network serialization", "[wifi]") {
    Fixture fixture;
    WifiConfigServiceImpl service(fixture.platform, fixture.options);
    service.Init();
    const auto id = service.Connect(fixture.Config()).at("operationId").get<std::string>();
    WaitFor(service, id, "awaitingConfirmation");
    REQUIRE(service.QueryStatus().at("operationId") == id);
    REQUIRE(service.QueryStatus().at("state") == "awaitingConfirmation");
    REQUIRE(service.QueryStatus().at("hasCredentials") == false);
    REQUIRE_THROWS_WITH(service.Scan(), "networkBusy");
    REQUIRE_FALSE(cosmo::service::NetworkMutationLease::TryAcquire());
    service.Confirm(id);
    REQUIRE(WaitFor(service, id).at("success") == true);
    WaitIdle();
    const auto status = service.QueryStatus();
    REQUIRE(status.at("state") == "standby");
    REQUIRE(status.at("autoConnect") == true);
    REQUIRE(status.at("hasCredentials") == true);
    REQUIRE(status.dump().find("psk") == std::string::npos);
    REQUIRE(status.dump().find("wifi-test-password") == std::string::npos);
    const auto metadata = cosmo::platform::ReadPrivateFile(fixture.options.metadata_file);
    REQUIRE(metadata.find("psk") == std::string::npos);
    REQUIRE(metadata.find("password") == std::string::npos);
    struct stat status_file {};
    REQUIRE(stat((fixture.options.private_directory + "/confirmed.json").c_str(), &status_file) == 0);
    REQUIRE((status_file.st_mode & 0777) == 0600);
    REQUIRE_FALSE(std::filesystem::exists(fixture.options.private_directory + "/pending.json"));
}
TEST_CASE("Wi-Fi failure and confirmation timeout restore previous configuration", "[wifi]") {
    Fixture fixture;
    WifiConfigServiceImpl service(fixture.platform, fixture.options);
    service.Init();
    ConfirmConnection(service, fixture.Config());
    SECTION("wrong key") {
        fixture.platform->fail_auth = true;
    }
    SECTION("DHCP timeout") {
        fixture.platform->address_available = false;
    }
    SECTION("apply failure") {
        fixture.platform->fail_apply = true;
    }
    SECTION("confirmation timeout") {}
    SECTION("address conflict") {
        fixture.platform->conflict = true;
    }
    auto next    = fixture.Config();
    next["ssid"] = "new AP";
    if (fixture.platform->conflict) {
        next["dhcp"]    = false;
        next["autoDns"] = false;
        next["dns"]     = Json::array({"192.0.2.1"});
        next["ipAddr"]  = "192.0.2.50";
        next["netMask"] = "255.255.255.0";
    }
    const auto id     = service.Connect(next).at("operationId").get<std::string>();
    const auto result = WaitFor(service, id);
    REQUIRE(result.at("stage") == "rolledBack");
    REQUIRE(result.at("rollback") == "restored");
    REQUIRE(result.at("success") == false);
    REQUIRE(result.at("error") != "interrupted");
    REQUIRE(fixture.platform->restore_count == 1);
    REQUIRE(service.QueryStatus().at("savedConfig").at("ssid") == "test AP");
    REQUIRE_FALSE(std::filesystem::exists(fixture.options.private_directory + "/pending.json"));
    WaitIdle();
}
TEST_CASE("Wi-Fi restart recovers an unconfirmed transaction", "[wifi]") {
    Fixture fixture;
    {
        WifiConfigServiceImpl service(fixture.platform, fixture.options);
        service.Init();
        ConfirmConnection(service, fixture.Config());
        auto next     = fixture.Config();
        next["ssid"]  = "unconfirmed AP";
        const auto id = service.Connect(next).at("operationId").get<std::string>();
        WaitFor(service, id, "awaitingConfirmation");
        service.Stop();
        REQUIRE(std::filesystem::exists(fixture.options.private_directory + "/pending.json"));
    }
    WifiConfigServiceImpl restarted(fixture.platform, fixture.options);
    restarted.Init();
    REQUIRE(fixture.platform->restore_count == 1);
    REQUIRE(restarted.QueryStatus().at("savedConfig").at("ssid") == "test AP");
    REQUIRE(restarted.QueryStatus().at("ssid") == "test AP");
    const auto recovered_id = restarted.QueryStatus().at("operationId").get<std::string>();
    REQUIRE(restarted.QueryOperation(recovered_id).at("error") == "interrupted");
    REQUIRE_FALSE(std::filesystem::exists(fixture.options.private_directory + "/pending.json"));
}
TEST_CASE("Wi-Fi disconnect retains credentials while forget and reset remove them", "[wifi]") {
    Fixture fixture;
    WifiConfigServiceImpl service(fixture.platform, fixture.options);
    service.Init();
    ConfirmConnection(service, fixture.Config());
    const auto disconnected = service.Disconnect().at("operationId").get<std::string>();
    REQUIRE(WaitFor(service, disconnected).at("success") == true);
    WaitIdle();
    REQUIRE(service.QueryStatus().at("autoConnect") == false);
    REQUIRE(service.QueryStatus().at("hasCredentials") == true);
    auto config = fixture.Config();
    config.erase("password");
    ConfirmConnection(service, config);
    SECTION("forget") {
        const auto forgotten = service.Forget().at("operationId").get<std::string>();
        REQUIRE(WaitFor(service, forgotten).at("success") == true);
        WaitIdle();
        REQUIRE(service.QueryStatus().at("hasCredentials") == false);
        REQUIRE_FALSE(std::filesystem::exists(fixture.options.private_directory + "/confirmed.json"));
    }
    SECTION("factory reset") {
        REQUIRE(service.Reset());
        REQUIRE(service.QueryStatus().at("hasCredentials") == false);
        REQUIRE_FALSE(std::filesystem::exists(fixture.options.private_directory));
    }
}
TEST_CASE("Wi-Fi failed recovery retains the journal for a later retry", "[wifi]") {
    Fixture fixture;
    WifiConfigServiceImpl service(fixture.platform, fixture.options);
    service.Init();
    fixture.platform->fail_auth    = true;
    fixture.platform->fail_restore = true;
    const auto id                  = service.Connect(fixture.Config()).at("operationId").get<std::string>();
    const auto result              = WaitFor(service, id);
    REQUIRE(result.at("rollback") == "failed");
    REQUIRE(service.QueryStatus().at("reason") == "rollbackFailed");
    REQUIRE(std::filesystem::exists(fixture.options.private_directory + "/pending.json"));
    WaitIdle();
}
TEST_CASE("Wi-Fi scan timeout is reported without claiming a connection", "[wifi]") {
    Fixture fixture;
    fixture.platform->emit_scan = false;
    WifiConfigServiceImpl service(fixture.platform, fixture.options);
    service.Init();
    const auto id     = service.Scan().at("operationId").get<std::string>();
    const auto result = WaitFor(service, id);
    REQUIRE(result.at("error") == "scanTimeout");
    REQUIRE(result.at("success") == false);
    WaitIdle();
}

TEST_CASE("Wi-Fi does not accept a previous completed association as the requested AP", "[wifi]") {
    Fixture fixture;
    WifiConfigServiceImpl service(fixture.platform, fixture.options);
    service.Init();
    ConfirmConnection(service, fixture.Config());
    fixture.platform->reported_ssid = "test AP";
    auto config                     = fixture.Config();
    config["ssid"]                  = "new AP";
    const auto id                   = service.Connect(config).at("operationId").get<std::string>();
    Json result;
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    do {
        result = service.QueryOperation(id);
        REQUIRE(result.at("stage") != "awaitingConfirmation");
        if (result.at("done").get<bool>())
            break;
        std::this_thread::sleep_for(1ms);
    } while (std::chrono::steady_clock::now() < deadline);
    REQUIRE(result.at("done") == true);
    REQUIRE(result.at("stage") == "rolledBack");
    REQUIRE(result.at("error") == "connectionTimeout");
    REQUIRE(service.QueryStatus().at("savedConfig").at("ssid") == "test AP");
    WaitIdle();
}
TEST_CASE("Wi-Fi decodes escaped STATUS SSIDs for display and connection matching", "[wifi]") {
    Fixture fixture;
    fixture.platform->reported_ssid = R"(\xe4\xb8\xad\xe6\x96\x87 AP \"\\)";
    WifiConfigServiceImpl service(fixture.platform, fixture.options);
    service.Init();
    auto config    = fixture.Config();
    config["ssid"] = "中文 AP \"\\";
    REQUIRE(service.QueryStatus().at("ssid") == config.at("ssid"));
    ConfirmConnection(service, config);
    REQUIRE(service.QueryStatus().at("ssid") == config.at("ssid"));
}

TEST_CASE("Wi-Fi default metadata path creates a file with no credentials", "[wifi]") {
    Fixture fixture;
    cosmo::test::ScopedPathOverride path_override(fixture.root + "/data", fixture.root + "/app");
    fixture.options.metadata_file.clear();
    const auto metadata_file = std::filesystem::path(cosmo::path::GetCfgPath()) / "wifi.json";
    WifiConfigServiceImpl service(fixture.platform, fixture.options);
    service.Init();
    ConfirmConnection(service, fixture.Config());
    REQUIRE(std::filesystem::is_regular_file(metadata_file));
    const auto metadata = cosmo::platform::ReadPrivateFile(metadata_file.string());
    REQUIRE(metadata.find("psk") == std::string::npos);
    REQUIRE(metadata.find("password") == std::string::npos);
    REQUIRE(Json::parse(metadata).at("config").at("ssid") == "test AP");
}

TEST_CASE("Wi-Fi DNS recovery uses the explicit link instead of saved IPv6 scope indices", "[wifi]") {
    const std::vector<std::string> dns{"192.0.2.53",   "fe80::1%127", "fe80::2%old-interface",
                                       "2001:db8::53", "invalid%127", "192.0.2.1%127"};
    const std::vector<std::string> expected{"192.0.2.53",   "fe80::1",     "fe80::2",
                                            "2001:db8::53", "invalid%127", "192.0.2.1%127"};
    REQUIRE(cosmo::platform::NormalizeResolvedValuesForLink("dns", dns) == expected);
    const std::vector<std::string> domains{"~.", "corp.example", "fe80::1%127"};
    REQUIRE(cosmo::platform::NormalizeResolvedValuesForLink("domain", domains) == domains);
}

TEST_CASE("Wi-Fi applying a configuration reconnects after an explicit disconnect", "[wifi]") {
    std::vector<std::string> commands;
    std::string failed_command;
    auto request = [&](const std::string& command) {
        commands.push_back(command);
        return command == failed_command ? "FAIL\n" : "OK\n";
    };
    SECTION("reload and reconnect both succeed") {
        REQUIRE_NOTHROW(cosmo::platform::ReconfigureAndReconnectWifi(request));
        REQUIRE(commands == std::vector<std::string>{"RECONFIGURE", "RECONNECT"});
    }
    SECTION("failed reload does not reconnect") {
        failed_command = "RECONFIGURE";
        REQUIRE_THROWS_WITH(cosmo::platform::ReconfigureAndReconnectWifi(request),
                            "authenticationConfigFailed");
        REQUIRE(commands == std::vector<std::string>{"RECONFIGURE"});
    }
    SECTION("failed reconnect is returned to the caller") {
        failed_command = "RECONNECT";
        REQUIRE_THROWS_WITH(cosmo::platform::ReconfigureAndReconnectWifi(request),
                            "authenticationConfigFailed");
        REQUIRE(commands == std::vector<std::string>{"RECONFIGURE", "RECONNECT"});
    }
}

TEST_CASE("Wi-Fi ARP conflict checks ignore all local interface MACs", "[wifi]") {
    const std::array<unsigned char, 4> candidate{192, 0, 2, 50};
    const std::array<unsigned char, 6> wired_mac{0x02, 0, 0, 0, 0, 1};
    const std::array<unsigned char, 6> wifi_mac{0x02, 0, 0, 0, 0, 2};
    const std::array<unsigned char, 6> foreign_mac{0x02, 0, 0, 0, 0, 3};
    const std::vector<std::array<unsigned char, 6>> local_macs{wired_mac, wifi_mac};
    std::array<unsigned char, 42> packet{};
    packet[12] = 0x08;
    packet[13] = 0x06;
    packet[15] = 1;
    packet[16] = 0x08;
    packet[18] = 6;
    packet[19] = 4;
    packet[21] = 2;
    std::copy(foreign_mac.begin(), foreign_mac.end(), packet.begin() + 22);
    std::copy(candidate.begin(), candidate.end(), packet.begin() + 28);
    SECTION("the local wired interface does not conflict with Wi-Fi") {
        std::copy(wired_mac.begin(), wired_mac.end(), packet.begin() + 22);
        REQUIRE_FALSE(
            cosmo::platform::IsConflictingArpPacket(packet.data(), packet.size(), candidate, local_macs));
    }
    SECTION("a foreign sender claiming the candidate conflicts") {
        REQUIRE(cosmo::platform::IsConflictingArpPacket(packet.data(), packet.size(), candidate, local_macs));
    }
    SECTION("a competing foreign probe conflicts") {
        packet[21] = 1;
        std::fill_n(packet.begin() + 28, 4, 0);
        std::copy(candidate.begin(), candidate.end(), packet.begin() + 38);
        REQUIRE(cosmo::platform::IsConflictingArpPacket(packet.data(), packet.size(), candidate, local_macs));
    }
    SECTION("an unrelated foreign address does not conflict") {
        packet[31] = 51;
        REQUIRE_FALSE(
            cosmo::platform::IsConflictingArpPacket(packet.data(), packet.size(), candidate, local_macs));
    }
    SECTION("short malformed or unsupported frames are ignored") {
        REQUIRE_FALSE(
            cosmo::platform::IsConflictingArpPacket(packet.data(), packet.size() - 1, candidate, local_macs));
        packet[13] = 0;
        REQUIRE_FALSE(
            cosmo::platform::IsConflictingArpPacket(packet.data(), packet.size(), candidate, local_macs));
        packet[13] = 0x06;
        packet[21] = 3;
        REQUIRE_FALSE(
            cosmo::platform::IsConflictingArpPacket(packet.data(), packet.size(), candidate, local_macs));
    }
}
