// Network card configuration utilities.
// Provides IP/DHCP/DNS management via netplan and ifconfig.
#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

#include "util/ErrorCode.h"

namespace cosmo::platform {

constexpr const char* kNetworkMainCardName = "WAN";
constexpr const char* kNetworkSubCardName  = "LAN";

constexpr const char* kNetworkMainEthName = "eth0";
constexpr const char* kNetworkSubEthName  = "eth1";

struct NetCardInfo {
    bool is_dhcp_change{true};
    bool is_main{false};
    int dhcp{0};
    std::string eth_name;
    std::string ip_addr;
    std::string net_mask;
    std::string gateway;
    std::string mac;
    std::vector<std::string> dnss;
};

void to_json(nlohmann::json& j, const NetCardInfo& v);
void from_json(const nlohmann::json& j, NetCardInfo& v);

struct NetCardOpResult {
    cosmo::util::ErrorEnum error_code{cosmo::util::ErrorEnum::Success};
    std::string message;
    std::string result_data;
};

NetCardOpResult DoNetCard(const NetCardInfo& info);
int DoNetCards(const NetCardInfo& main, const NetCardInfo& sub);

// Set DNS
void DnsEffect(const std::vector<std::string>& dns_servers);

// Get network interface info
std::vector<NetCardInfo> GetIpInfos();
NetCardInfo GetIpInfo(const std::string& eth_name);

std::string GetIPString(uint32_t ip_value);

}  // namespace cosmo::platform
