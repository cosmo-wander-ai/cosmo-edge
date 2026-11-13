// Asynchronous Wi-Fi transaction controller with persistent crash recovery.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "platform/WifiPlatform.h"
#include "service/network/IWifiConfig.h"
#include "service/network/NetworkMutation.h"

namespace cosmo::service {
struct WifiServiceOptions {
    std::string private_directory{"/etc/cosmo-wifi"};
    std::string metadata_file;
    std::chrono::milliseconds scan_timeout{15000};
    std::chrono::milliseconds connect_timeout{60000};
    std::chrono::milliseconds confirm_timeout{90000};
    std::chrono::milliseconds poll_interval{250};
};

class WifiConfigServiceImpl final : public IWifiConfig {
public:
    WifiConfigServiceImpl();
    explicit WifiConfigServiceImpl(std::shared_ptr<platform::IWifiPlatform> platform,
                                   WifiServiceOptions options = {});
    ~WifiConfigServiceImpl() override;
    WifiConfigServiceImpl(const WifiConfigServiceImpl&)            = delete;
    WifiConfigServiceImpl& operator=(const WifiConfigServiceImpl&) = delete;
    void Init() override;
    void Stop() override;
    nlohmann::json QueryStatus() override;
    nlohmann::json Scan() override;
    nlohmann::json Connect(const nlohmann::json& config) override;
    nlohmann::json QueryOperation(const std::string& operation_id) override;
    nlohmann::json Confirm(const std::string& operation_id) override;
    nlohmann::json Disconnect() override;
    nlohmann::json Forget() override;
    bool Reset() override;

private:
    nlohmann::json StartOperation(const std::string& type, std::function<void()> action);
    void ScanWorker();
    void ConnectWorker(nlohmann::json config);
    void ClearWorker(bool forget);
    void ChangeStage(const std::string& stage);
    void Complete(bool success, const std::string& stage, const std::string& error = "",
                  const std::string& rollback = "");
    void SaveConfirmed(const nlohmann::json& config, bool auto_connect);
    nlohmann::json LoadJson(const std::string& name) const;
    void SaveJson(const std::string& name, const nlohmann::json& value) const;
    void Remove(const std::string& name) const;
    void RestorePending(bool report_interrupted = true);
    void EnsureReady();
    void UpdateDns();
    std::string Path(const std::string& name) const;
    static nlohmann::json PublicConfig(nlohmann::json config);

    std::shared_ptr<platform::IWifiPlatform> platform_;
    WifiServiceOptions options_;
    std::mutex mutex_;
    std::mutex lifecycle_mutex_;
    std::condition_variable changed_;
    std::thread worker_;
    std::thread monitor_;
    std::atomic<bool> stopped_{false};
    std::string interface_name_;
    std::string reason_;
    std::string dns_interface_{"#uninitialized"};
    nlohmann::json confirmed_ = nlohmann::json::object();
    nlohmann::json operation_ = nlohmann::json::object();
    bool auto_connect_{false};
    bool confirmation_requested_{false};
    bool initialized_{false};
    std::chrono::steady_clock::time_point confirm_deadline_;
};
}  // namespace cosmo::service
