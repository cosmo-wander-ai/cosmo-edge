// Native runtime interface and supplicant/networkd operations; no shell strings.
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace cosmo::platform {
struct NetworkInterfaceState {
    std::string interface_name;
    std::string type;
    bool link_up{false};
    std::string ip_addr;
    std::string net_mask;
    std::string gateway;
    bool is_default{false};
    int metric{-1};
};
std::vector<NetworkInterfaceState> ReadNetworkInterfaces();
std::string CurrentHostAddress();
std::string AddressForPeer(const std::string& ipv4);
bool HasWirelessInterface();
bool SetResolvedDns(const std::string& interface_name, const std::vector<std::string>& dns);
nlohmann::json NetworkInterfacesJson(const std::vector<NetworkInterfaceState>& interfaces);
void WritePrivateFile(const std::string& path, const std::string& data);
std::string ReadPrivateFile(const std::string& path);
nlohmann::json ValidateWifiSettings(const nlohmann::json& input, const nlohmann::json& saved);
std::string WifiSupplicantConfig(const nlohmann::json& settings, bool auto_connect);
std::string WifiNetworkConfig(const std::string& interface_name, const nlohmann::json& settings);
std::string DecodeWifiSsid(const std::string& encoded);
std::vector<std::string> NormalizeResolvedValuesForLink(const std::string& property,
                                                        const std::vector<std::string>& values);
nlohmann::json ParseWifiScanResults(const std::string& response);
bool IsConflictingArpPacket(const unsigned char* packet, size_t length,
                            const std::array<unsigned char, 4>& candidate,
                            const std::vector<std::array<unsigned char, 6>>& local_macs);
void ReconfigureAndReconnectWifi(const std::function<std::string(const std::string&)>& request);

class IWifiPlatform {
public:
    virtual ~IWifiPlatform()                                                                   = default;
    virtual std::vector<NetworkInterfaceState> Interfaces()                                    = 0;
    virtual bool IsSupported() const                                                           = 0;
    virtual nlohmann::json Snapshot(const std::string& interface_name)                         = 0;
    virtual void Prepare(const std::string& interface_name)                                    = 0;
    virtual std::string Request(const std::string& interface_name, const std::string& command) = 0;
    virtual std::string WaitEvent(const std::string& interface_name, std::chrono::milliseconds timeout) = 0;
    virtual void Apply(const std::string& interface_name, const nlohmann::json& settings)               = 0;
    virtual void Restore(const std::string& interface_name, const nlohmann::json& snapshot)             = 0;
    virtual void Commit(const std::string& interface_name, const nlohmann::json& settings,
                        bool auto_connect)                                                              = 0;
    virtual void Clear(const std::string& interface_name, bool forget)                                  = 0;
    virtual bool HasAddressConflict(const std::string& interface_name, const std::string& address)      = 0;
    virtual void UpdateDnsRoute(const std::string& current_interface)                                   = 0;
};

struct WifiPlatformPaths {
    std::string supplicant_dir{"/etc/wpa_supplicant"};
    std::string network_file{"/etc/systemd/network/20-cosmo-wifi.network"};
    std::string control_dir{"/run/wpa_supplicant"};
};
std::shared_ptr<IWifiPlatform> MakeWifiPlatform();
}  // namespace cosmo::platform
