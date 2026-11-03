#include "service/network/impl/WifiConfigServiceImpl.h"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>

#include "util/PathUtil.h"

namespace cosmo::service {
namespace {
    std::atomic<unsigned long long> operation_sequence{0};
    std::string StatusValue(const std::string& status, const std::string& key) {
        const auto marker = key + "=";
        auto begin        = status.find(marker);
        while (begin != std::string::npos && begin != 0 && status[begin - 1] != '\n')
            begin = status.find(marker, begin + marker.size());
        if (begin == std::string::npos)
            return {};
        begin += marker.size();
        return status.substr(begin, status.find('\n', begin) - begin);
    }
    bool SafeError(const std::string& value) {
        return !value.empty() && value.size() <= 80 &&
               std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isalnum(c); });
    }
}  // namespace

WifiConfigServiceImpl::WifiConfigServiceImpl() : WifiConfigServiceImpl(platform::MakeWifiPlatform()) {}
WifiConfigServiceImpl::WifiConfigServiceImpl(std::shared_ptr<platform::IWifiPlatform> platform,
                                             WifiServiceOptions options)
    : platform_(std::move(platform)), options_(std::move(options)) {
    if (options_.metadata_file.empty())
        options_.metadata_file = (std::filesystem::path(path::GetCfgPath()) / "wifi.json").string();
}
WifiConfigServiceImpl::~WifiConfigServiceImpl() {
    WifiConfigServiceImpl::Stop();
}
std::string WifiConfigServiceImpl::Path(const std::string& name) const {
    return (std::filesystem::path(options_.private_directory) / name).string();
}
nlohmann::json WifiConfigServiceImpl::LoadJson(const std::string& name) const {
    const auto file = Path(name);
    if (!std::filesystem::exists(file))
        return nlohmann::json::object();
    try {
        return nlohmann::json::parse(platform::ReadPrivateFile(file));
    } catch (...) {
        throw std::runtime_error("loadFailed");
    }
}
void WifiConfigServiceImpl::SaveJson(const std::string& name, const nlohmann::json& value) const {
    platform::WritePrivateFile(Path(name), value.dump());
}
void WifiConfigServiceImpl::Remove(const std::string& name) const {
    std::filesystem::remove(Path(name));
}
nlohmann::json WifiConfigServiceImpl::PublicConfig(nlohmann::json config) {
    config.erase("password");
    config.erase("psk");
    return config;
}
void WifiConfigServiceImpl::SaveConfirmed(const nlohmann::json& config, bool auto_connect) {
    SaveJson("confirmed.json", {{"config", config}, {"autoConnect", auto_connect}});
    platform::WritePrivateFile(
        options_.metadata_file,
        nlohmann::json{{"config", PublicConfig(config)}, {"autoConnect", auto_connect}}.dump());
    std::lock_guard<std::mutex> lock(mutex_);
    confirmed_    = config;
    auto_connect_ = auto_connect;
}
void WifiConfigServiceImpl::RestorePending(bool report_interrupted) {
    const auto pending = LoadJson("pending.json");
    if (pending.empty())
        return;
    const auto name = pending.at("interfaceName").get<std::string>();
    if (name != interface_name_)
        throw std::runtime_error("rollbackInterfaceUnavailable");
    platform_->Restore(name, pending.at("snapshot"));
    const auto old = pending.at("confirmedBefore");
    if (old.empty()) {
        Remove("confirmed.json");
        std::filesystem::remove(options_.metadata_file);
        std::lock_guard<std::mutex> lock(mutex_);
        confirmed_    = nlohmann::json::object();
        auto_connect_ = false;
    } else {
        SaveConfirmed(old.at("config"), old.value("autoConnect", false));
    }
    Remove("pending.json");
    if (!report_interrupted)
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    operation_ = {{"operationId", pending.value("operationId", "")},
                  {"type", pending.value("type", "connect")},
                  {"stage", "rolledBack"},
                  {"done", true},
                  {"success", false},
                  {"error", "interrupted"},
                  {"rollback", "restored"},
                  {"confirmSeconds", 0},
                  {"ipAddr", ""},
                  {"scanResults", nlohmann::json::array()}};
}
void WifiConfigServiceImpl::Init() {
    std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
    if (initialized_)
        return;
    initialized_ = true;
    if (!platform_->IsSupported())
        return;
    for (const auto& info : platform_->Interfaces())
        if (info.type == "wifi") {
            interface_name_ = info.interface_name;
            break;
        }
    if (interface_name_.empty())
        return;
    auto lease = NetworkMutationLease::TryAcquire();
    if (!lease) {
        reason_ = "networkBusy";
        return;
    }
    try {
        std::filesystem::create_directories(options_.private_directory);
        if (chmod(options_.private_directory.c_str(), 0700) != 0)
            throw std::runtime_error("saveFailed");
        const auto saved = LoadJson("confirmed.json");
        if (!saved.empty()) {
            confirmed_    = saved.at("config");
            auto_connect_ = saved.value("autoConnect", false);
        }
        RestorePending();
        if (!std::filesystem::exists(Path("baseline.json")))
            SaveJson("baseline.json", {{"interfaceName", interface_name_},
                                       {"snapshot", platform_->Snapshot(interface_name_)}});
        // Starting an existing instance leaves its established connection intact.
        platform_->Prepare(interface_name_);
        if (!confirmed_.empty() && !auto_connect_)
            platform_->Clear(interface_name_, false);
        UpdateDns();
    } catch (const std::exception& error) {
        reason_ = SafeError(error.what()) ? error.what() : "serviceUnavailable";
    }
    monitor_ = std::thread([this] {
        while (!stopped_) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                changed_.wait_for(lock, std::chrono::seconds(1), [this] { return stopped_.load(); });
            }
            if (stopped_)
                break;
            auto monitor_lease = NetworkMutationLease::TryAcquire();
            if (!monitor_lease)
                continue;
            try {
                UpdateDns();
            } catch (...) {
                std::lock_guard<std::mutex> lock(mutex_);
                reason_ = "dnsConfigFailed";
            }
        }
    });
}
void WifiConfigServiceImpl::Stop() {
    std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
    stopped_ = true;
    changed_.notify_all();
    if (worker_.joinable())
        worker_.join();
    if (monitor_.joinable())
        monitor_.join();
}
void WifiConfigServiceImpl::EnsureReady() {
    if (stopped_)
        throw std::runtime_error("serviceStopping");
    if (interface_name_.empty())
        throw std::runtime_error("wifiUnavailable");
    std::lock_guard<std::mutex> lock(mutex_);
    if (!reason_.empty())
        throw std::runtime_error(reason_);
}
nlohmann::json WifiConfigServiceImpl::QueryStatus() {
    const auto interfaces = platform_->Interfaces();
    nlohmann::json result{{"available", platform_->IsSupported() && !interface_name_.empty()},
                          {"serviceReady", false},
                          {"reason", ""},
                          {"busy", NetworkMutationLease::IsBusy()},
                          {"interfaceName", interface_name_},
                          {"state", "unavailable"},
                          {"ssid", ""},
                          {"hasCredentials", false},
                          {"autoConnect", false},
                          {"savedConfig", nlohmann::json::object()},
                          {"interfaces", platform::NetworkInterfacesJson(interfaces)},
                          {"currentInterface", ""},
                          {"currentAddress", ""},
                          {"localOnly", true},
                          {"operationId", ""}};
    for (const auto& info : interfaces) {
        if (info.is_default) {
            result["currentInterface"] = info.interface_name;
            result["currentAddress"]   = info.ip_addr;
            result["localOnly"]        = false;
        }
    }
    if (result.at("currentAddress") == "") {
        for (const auto& info : interfaces)
            if (info.link_up && !info.ip_addr.empty()) {
                result["currentAddress"] = info.ip_addr;
                break;
            }
    }
    std::string stage;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        result["reason"]         = reason_;
        result["hasCredentials"] = confirmed_.contains("psk");
        result["savedConfig"]    = PublicConfig(confirmed_);
        result["autoConnect"]    = auto_connect_;
        result["operationId"]    = operation_.value("operationId", "");
        if (!operation_.value("done", true))
            stage = operation_.value("stage", "");
    }
    if (interface_name_.empty())
        return result;
    try {
        const auto status = platform_->Request(interface_name_, "STATUS");
        if (status.find("wpa_state=") == std::string::npos)
            throw std::runtime_error("serviceUnavailable");
        result["serviceReady"] = result.at("reason") == "";
        result["state"]        = "disconnected";
        result["ssid"]         = platform::DecodeWifiSsid(StatusValue(status, "ssid"));
        if (StatusValue(status, "wpa_state") == "COMPLETED") {
            const auto found = std::find_if(interfaces.begin(), interfaces.end(), [&](const auto& info) {
                return info.interface_name == interface_name_ && info.link_up && !info.ip_addr.empty();
            });
            result["state"] =
                found == interfaces.end() ? "addressing" : (found->is_default ? "connected" : "standby");
        } else if (StatusValue(status, "wpa_state") != "DISCONNECTED" &&
                   StatusValue(status, "wpa_state") != "INACTIVE" &&
                   StatusValue(status, "wpa_state") != "INTERFACE_DISABLED") {
            result["state"] = "connecting";
        }
        if (stage == "connecting" || stage == "addressing" || stage == "awaitingConfirmation")
            result["state"] = stage;
    } catch (...) {
        result["state"]  = "serviceUnavailable";
        result["reason"] = "serviceUnavailable";
    }
    return result;
}
nlohmann::json WifiConfigServiceImpl::StartOperation(const std::string& type, std::function<void()> action) {
    auto lease = NetworkMutationLease::TryAcquire();
    if (!lease)
        throw std::runtime_error("networkBusy");
    std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
    EnsureReady();
    if (worker_.joinable())
        worker_.join();
    const auto id = std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + "-" +
                    std::to_string(operation_sequence.fetch_add(1));
    {
        std::lock_guard<std::mutex> lock(mutex_);
        confirmation_requested_ = false;
        operation_              = {{"operationId", id},
                                   {"type", type},
                                   {"stage", type == "scan" ? "scanning" : "connecting"},
                                   {"done", false},
                                   {"success", false},
                                   {"error", ""},
                                   {"rollback", ""},
                                   {"confirmSeconds", 0},
                                   {"ipAddr", ""},
                                   {"scanResults", nlohmann::json::array()}};
    }
    worker_ = std::thread([this, action = std::move(action), lease = std::move(lease)] {
        try {
            action();
        } catch (const std::exception& error) {
            Complete(false, "failed", SafeError(error.what()) ? error.what() : "operationFailed");
        } catch (...) {
            Complete(false, "failed", "operationFailed");
        }
    });
    return {{"operationId", id}};
}
void WifiConfigServiceImpl::ChangeStage(const std::string& stage) {
    std::lock_guard<std::mutex> lock(mutex_);
    operation_["stage"] = stage;
}
void WifiConfigServiceImpl::Complete(bool success, const std::string& stage, const std::string& error,
                                     const std::string& rollback) {
    std::lock_guard<std::mutex> lock(mutex_);
    operation_["stage"]          = stage;
    operation_["done"]           = true;
    operation_["success"]        = success;
    operation_["error"]          = error;
    operation_["rollback"]       = rollback;
    operation_["confirmSeconds"] = 0;
}
nlohmann::json WifiConfigServiceImpl::Scan() {
    return StartOperation("scan", [this] { ScanWorker(); });
}
void WifiConfigServiceImpl::ScanWorker() {
    platform_->Prepare(interface_name_);
    for (int i = 0; i < 256; ++i)
        if (platform_->WaitEvent(interface_name_, std::chrono::milliseconds(0)).empty())
            break;
    const auto result = platform_->Request(interface_name_, "SCAN");
    if (result.find("OK") != 0)
        throw std::runtime_error("scanFailed");
    const auto deadline = std::chrono::steady_clock::now() + options_.scan_timeout;
    while (!stopped_ && std::chrono::steady_clock::now() < deadline) {
        const auto event = platform_->WaitEvent(interface_name_, options_.poll_interval);
        if (event.find("CTRL-EVENT-SCAN-FAILED") != std::string::npos)
            throw std::runtime_error("scanFailed");
        if (event.find("CTRL-EVENT-SCAN-RESULTS") == std::string::npos)
            continue;
        auto results = platform::ParseWifiScanResults(platform_->Request(interface_name_, "SCAN_RESULTS"));
        {
            std::lock_guard<std::mutex> lock(mutex_);
            operation_["scanResults"] = std::move(results);
        }
        Complete(true, "succeeded");
        return;
    }
    Complete(false, "failed", stopped_ ? "interrupted" : "scanTimeout");
}
nlohmann::json WifiConfigServiceImpl::Connect(const nlohmann::json& config) {
    nlohmann::json saved;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        saved = confirmed_;
    }
    auto settings = platform::ValidateWifiSettings(config, saved);
    return StartOperation("connect", [this, settings = std::move(settings)] { ConnectWorker(settings); });
}
void WifiConfigServiceImpl::ConnectWorker(nlohmann::json config) {
    nlohmann::json old;
    std::string operation_id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!confirmed_.empty())
            old = {{"config", confirmed_}, {"autoConnect", auto_connect_}};
        operation_id = operation_.at("operationId");
    }
    SaveJson("pending.json", {{"interfaceName", interface_name_},
                              {"snapshot", platform_->Snapshot(interface_name_)},
                              {"confirmedBefore", old},
                              {"operationId", operation_id}});
    try {
        platform_->Prepare(interface_name_);
        for (int i = 0; i < 256; ++i)
            if (platform_->WaitEvent(interface_name_, std::chrono::milliseconds(0)).empty())
                break;
        platform_->Apply(interface_name_, config);
        const auto deadline = std::chrono::steady_clock::now() + options_.connect_timeout;
        bool connected      = false;
        while (!stopped_ && std::chrono::steady_clock::now() < deadline) {
            const auto event = platform_->WaitEvent(interface_name_, options_.poll_interval);
            if (event.find("WRONG_KEY") != std::string::npos ||
                event.find("AUTH-REJECT") != std::string::npos ||
                event.find("SSID-TEMP-DISABLED") != std::string::npos)
                throw std::runtime_error("authenticationFailed");
            const auto status = platform_->Request(interface_name_, "STATUS");
            if (StatusValue(status, "wpa_state") != "COMPLETED" ||
                platform::DecodeWifiSsid(StatusValue(status, "ssid")) != config.at("ssid").get<std::string>())
                continue;
            ChangeStage("addressing");
            const auto interfaces = platform_->Interfaces();
            for (const auto& info : interfaces) {
                if (info.interface_name != interface_name_ || !info.link_up || info.ip_addr.empty())
                    continue;
                if (!config.at("dhcp").get<bool>() &&
                    (info.ip_addr != config.at("ipAddr") || info.net_mask != config.at("netMask")))
                    continue;
                if (!config.at("dhcp").get<bool>() &&
                    platform_->HasAddressConflict(interface_name_, info.ip_addr))
                    throw std::runtime_error("addressConflict");
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    operation_["ipAddr"] = info.ip_addr;
                }
                connected = true;
                break;
            }
            if (connected)
                break;
        }
        if (stopped_)
            return;  // The persisted journal is restored by the next process.
        if (!connected)
            throw std::runtime_error("connectionTimeout");
        UpdateDns();
        {
            std::unique_lock<std::mutex> lock(mutex_);
            operation_["stage"] = "awaitingConfirmation";
            confirm_deadline_   = std::chrono::steady_clock::now() + options_.confirm_timeout;
            changed_.wait_until(lock, confirm_deadline_,
                                [this] { return confirmation_requested_ || stopped_.load(); });
            if (stopped_)
                return;
            if (!confirmation_requested_)
                throw std::runtime_error("confirmationTimeout");
        }
        platform_->Commit(interface_name_, config, true);
        SaveConfirmed(config, true);
        Remove("pending.json");
        Complete(true, "succeeded");
    } catch (const std::exception& error) {
        const std::string failure = SafeError(error.what()) ? error.what() : "connectionFailed";
        try {
            RestorePending(false);
            dns_interface_.clear();
            UpdateDns();
            Complete(false, "rolledBack", failure, "restored");
        } catch (...) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                reason_ = "rollbackFailed";
            }
            Complete(false, "failed", failure, "failed");
        }
    }
}
nlohmann::json WifiConfigServiceImpl::QueryOperation(const std::string& operation_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (operation_.empty() || operation_.value("operationId", "") != operation_id)
        throw std::runtime_error("operationNotFound");
    auto result = operation_;
    if (operation_.value("stage", "") == "awaitingConfirmation" && !operation_.value("done", true)) {
        result["confirmSeconds"] =
            std::max<long long>(0, std::chrono::duration_cast<std::chrono::seconds>(
                                       confirm_deadline_ - std::chrono::steady_clock::now())
                                       .count());
    }
    return result;
}
nlohmann::json WifiConfigServiceImpl::Confirm(const std::string& operation_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (operation_.value("operationId", "") != operation_id ||
        operation_.value("stage", "") != "awaitingConfirmation" || operation_.value("done", true) ||
        std::chrono::steady_clock::now() >= confirm_deadline_)
        throw std::runtime_error("operationNotConfirmable");
    confirmation_requested_ = true;
    changed_.notify_all();
    return {{"operationId", operation_id}};
}
nlohmann::json WifiConfigServiceImpl::Disconnect() {
    return StartOperation("disconnect", [this] { ClearWorker(false); });
}
nlohmann::json WifiConfigServiceImpl::Forget() {
    return StartOperation("forget", [this] { ClearWorker(true); });
}
void WifiConfigServiceImpl::ClearWorker(bool forget) {
    nlohmann::json config;
    nlohmann::json old;
    std::string operation_id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config = confirmed_;
        if (!confirmed_.empty())
            old = {{"config", confirmed_}, {"autoConnect", auto_connect_}};
        operation_id = operation_.at("operationId");
    }
    SaveJson("pending.json", {{"interfaceName", interface_name_},
                              {"snapshot", platform_->Snapshot(interface_name_)},
                              {"confirmedBefore", old},
                              {"operationId", operation_id},
                              {"type", forget ? "forget" : "disconnect"}});
    try {
        if (!config.empty()) {
            // Disable before disconnecting so process/device restart cannot reconnect.
            platform_->Commit(interface_name_, config, false);
            SaveConfirmed(config, false);
        }
        platform_->Clear(interface_name_, forget);
        if (forget) {
            Remove("confirmed.json");
            std::filesystem::remove(options_.metadata_file);
            std::lock_guard<std::mutex> lock(mutex_);
            confirmed_    = nlohmann::json::object();
            auto_connect_ = false;
        }
        UpdateDns();
        Remove("pending.json");
        Complete(true, "succeeded");
    } catch (const std::exception& error) {
        const std::string failure = SafeError(error.what()) ? error.what() : "operationFailed";
        try {
            RestorePending(false);
            dns_interface_ = "#uninitialized";
            UpdateDns();
            Complete(false, "rolledBack", failure, "restored");
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex_);
            reason_                = "rollbackFailed";
            operation_["stage"]    = "failed";
            operation_["done"]     = true;
            operation_["success"]  = false;
            operation_["error"]    = failure;
            operation_["rollback"] = "failed";
        }
    }
}
void WifiConfigServiceImpl::UpdateDns() {
    std::string current;
    for (const auto& info : platform_->Interfaces())
        if (info.is_default)
            current = info.interface_name;
    if (current == dns_interface_)
        return;
    platform_->UpdateDnsRoute(current);
    dns_interface_ = current;
    std::lock_guard<std::mutex> lock(mutex_);
    if (reason_ == "dnsConfigFailed")
        reason_.clear();
}
bool WifiConfigServiceImpl::Reset() {
    auto lease = NetworkMutationLease::TryAcquire();
    if (!lease)
        return false;
    std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
    if (interface_name_.empty())
        return true;
    try {
        const auto baseline = LoadJson("baseline.json");
        platform_->Clear(interface_name_, true);
        if (!baseline.empty())
            platform_->Restore(interface_name_, baseline.at("snapshot"));
        std::filesystem::remove(options_.metadata_file);
        std::filesystem::remove_all(options_.private_directory);
        std::lock_guard<std::mutex> lock(mutex_);
        confirmed_    = nlohmann::json::object();
        auto_connect_ = false;
        return true;
    } catch (...) {
        return false;
    }
}
}  // namespace cosmo::service
