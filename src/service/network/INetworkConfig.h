/// @file INetworkConfig.h
/// @brief Network card and DNS configuration interface.
///        ISP split from INetworkService.
///        Consumed by api/MessageNetworkHandler for network config operations.
#pragma once

#include <string>
#include <vector>

#include "platform/NetCardOp.h"
#include "util/ErrorCode.h"

namespace cosmo::service {

/// Network card configuration view (simplified for API responses).
struct NetCardView {
    bool isMain{false};   ///< Whether this is the primary network interface.
    bool dhcp{false};     ///< Whether DHCP is enabled.
    std::string ethName;  ///< Linux network interface name (e.g. "eth0").
    std::string ipAddr;   ///< IPv4 address.
    std::string netMask;  ///< Subnet mask.
    std::string gateway;  ///< Default gateway.
};

/// Manages network card configuration, DNS settings, and IP address queries.
///
/// Provides both immediate and async configuration application (the async
/// variant reboots the device after applying to ensure iptables and routing
/// are reloaded).
class INetworkConfig {
public:
    virtual ~INetworkConfig() = default;

    // ── Network Card Configuration ──

    /// Initialize network card configuration from persistent storage.
    virtual void Init() = 0;

    /// Get the current hardware network card information.
    /// @param main If true, return the main (WAN) interface; otherwise LAN.
    /// @return Network card info structure.
    virtual cosmo::platform::NetCardInfo GetCardRealInfo(bool main = true) = 0;

    /// Get all network card hardware information.
    /// @return Vector of all network card info structures.
    virtual std::vector<cosmo::platform::NetCardInfo> GetCardRealInfos() = 0;

    /// Apply network card configuration immediately.
    /// @param info Network card configuration to apply.
    /// @return true on success.
    virtual bool SetCardInfo(const cosmo::platform::NetCardInfo& info) = 0;

    /// Asynchronously apply network card configuration and reboot on success.
    /// @param info Network card configuration to apply.
    virtual void ApplyCardInfoAsync(const cosmo::platform::NetCardInfo& info) = 0;

    /// Get currently configured DNS servers.
    /// @return Vector of DNS server IP addresses.
    virtual std::vector<std::string> GetCfgDns() = 0;

    /// Set DNS server configuration.
    /// @param dnss List of DNS server IP addresses.
    /// @return true on success.
    virtual bool SetDnss(std::vector<std::string> dnss) = 0;

    /// Search for and apply network card configuration with DNS.
    /// @param netCard [in/out] Network card info to search and update.
    /// @param dns1    Primary DNS server.
    /// @param dns2    Secondary DNS server.
    /// @return true on success.
    virtual bool SearchSetNewInfo(cosmo::platform::NetCardInfo& netCard, const std::string& dns1,
                                  const std::string& dns2) = 0;

    // ── Network Card Query ──

    /// Get a simplified view of all network interfaces.
    /// @return Vector of network card view structures.
    virtual std::vector<NetCardView> GetNetCards() = 0;

    /// Returns the IP address of the main network card.
    /// @return IPv4 address string.
    virtual std::string GetHostIpAddress() = 0;

    // ── Network Probe ──

    /// Result of a network quality probe (ping).
    struct PingQualityResult {
        float lost_rate{100.0f};      ///< Packet loss rate (0-100%).
        float average_latency{0.0f};  ///< Average round-trip time in ms.
        bool success{false};          ///< Whether the probe completed successfully.
    };

    /// Probe network quality by pinging the target host.
    /// @param ip         Target IP or hostname.
    /// @param packet_size  Ping packet size in bytes.
    /// @return Probe result with loss rate and average latency.
    virtual PingQualityResult ProbeNetworkQuality(const std::string& ip, int packet_size) = 0;

    /// Check if an IP address is reachable via single ping.
    /// @param ip  Target IP or hostname.
    /// @return true if reachable.
    virtual bool IsIpAccessible(const std::string& ip) = 0;
};

/// Linux interface name for the main (WAN) network card.
inline constexpr const char* kMainCardName = "WAN";

/// Linux device name for the main network interface.
inline constexpr const char* kMainEthName = "eth0";

}  // namespace cosmo::service
