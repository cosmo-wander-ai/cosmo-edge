// NetCardOpConfig.cc — Network configuration for NetCardOp.
// Split from NetCardOp.cc to reduce file size (DEBT-007).

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>

#include "platform/NetCardOp.h"
#include "platform/NetCardOpInternal.h"
#include "platform/NetworkManagerConfig.h"
#include "platform/PlatformConstants.h"
#include "util/Exec.h"
#include "util/FileUtil.h"
#include "util/Log.h"

namespace cosmo::platform {

// Forward declarations — defined in NetCardOp.cc
bool DoNetplanFailSafeFile();
bool ModifyNetCfg(const NetCardInfo& main, const NetCardInfo& sub);

namespace internal = cosmo::platform::internal;

#ifdef COSMO_NN_USE_RKNN_BACKEND
bool internal::ApplyNetworkManagerCard(const NetCardInfo& info) {
    if (!internal::IsValidNetworkName(info.eth_name) || (info.dhcp != 0 && info.dhcp != 1) ||
        (!info.gateway.empty() && !internal::IsValidIpAddress(info.gateway))) {
        LOG_WARN("Invalid NetworkManager configuration for {}", info.eth_name);
        return false;
    }
    std::string address;
    if (!info.dhcp) {
        if (!internal::IsValidIpAddress(info.ip_addr) || !internal::IsValidSubnetMask(info.net_mask)) {
            LOG_WARN("Invalid NetworkManager address or mask for {}", info.eth_name);
            return false;
        }
        in_addr mask{};
        inet_pton(AF_INET, info.net_mask.c_str(), &mask);
        auto bits       = ntohl(mask.s_addr);
        unsigned prefix = 0;
        while (bits) {
            ++prefix;
            bits <<= 1;
        }
        address = info.ip_addr + "/" + std::to_string(prefix);
    }
    std::string dns;
    for (const auto& server : info.dnss) {
        if (!internal::IsValidIpAddress(server))
            return false;
        if (!dns.empty())
            dns += ",";
        dns += server;
    }
    internal::NetworkManagerConfig manager(
        [](const auto& argv, auto& out) { return cosmo::util::Exec(argv, out); });
    if (!manager.Apply(info.eth_name, info.dhcp != 0, address, info.gateway, dns, info.is_main)) {
        LOG_WARN("NetworkManager apply failed for {}: {}", info.eth_name, manager.Error());
        return false;
    }
    LOG_INFO("NetworkManager applied configuration for {}", info.eth_name);
    return true;
}
#endif

int DoNetCards(const NetCardInfo& main, const NetCardInfo& sub) {
#ifdef COSMO_NN_USE_RKNN_BACKEND
    // Single-port boards do not have eth1.
    if (if_nametoindex(main.eth_name.c_str()) == 0) {
        LOG_WARN("Main network interface {} is missing", main.eth_name);
        return -1;
    }
    if (!internal::ApplyNetworkManagerCard(main))
        return -1;
    if (if_nametoindex(sub.eth_name.c_str()) != 0 && !internal::ApplyNetworkManagerCard(sub))
        return -1;
    return 0;
#elif !defined(COSMO_NN_USE_SOPHON_BACKEND)
    LOG_WARN("{}", "Network card configuration is not supported on x86 platform.");
    return -1;
#else
    auto net_cards     = GetIpInfos();
    bool need_update   = false;
    bool is_main_found = false;
    bool is_sub_found  = false;
    for (const auto& real_net : net_cards) {
        LOG_INFO("Real NetCard:{} DHCP/IP/Mask/Gateway  {}/{}/{}/{}", real_net.eth_name, real_net.dhcp,
                 real_net.ip_addr, real_net.net_mask, real_net.gateway);
        if (real_net.is_main) {
            if ((real_net.dhcp != main.dhcp) || (real_net.ip_addr != main.ip_addr) ||
                (real_net.net_mask != main.net_mask) || (real_net.gateway != main.gateway)) {
                LOG_INFO("NetCard Update Effect! main:{}/{}/{}<->{}/{}/{} | sub:{}/{}/{}<->{}/{}/{}",
                         real_net.eth_name, real_net.dhcp, real_net.ip_addr, real_net.net_mask,
                         real_net.gateway, main.dhcp, main.ip_addr, main.net_mask, main.gateway, sub.eth_name,
                         sub.dhcp, sub.ip_addr, sub.net_mask, sub.gateway);
                need_update = true;
            }
            is_main_found = true;
        } else {
            // Subnet interface has no gateway
            if ((real_net.dhcp != sub.dhcp) || (real_net.ip_addr != sub.ip_addr) ||
                (real_net.net_mask != sub.net_mask)) {
                LOG_INFO("NetCard:{} DHCP/IP/Mask/Gateway Change From {}/{}/{}/{} To {}/{}/{}/{}",
                         real_net.eth_name, real_net.dhcp, real_net.ip_addr, real_net.net_mask,
                         real_net.gateway, sub.dhcp, sub.ip_addr, sub.net_mask, sub.gateway);
                need_update = true;
            }
            is_sub_found = true;
        }
    }

    LOG_INFO("need_update: {}, is_main_found: {}, is_sub_found: {}", need_update, is_main_found,
             is_sub_found);

    if ((!need_update) && (is_main_found) && (is_sub_found)) {
        LOG_INFO("{}", "NetWork Have No Change.");
        return 1;
    }

    if (!DoNetplanFailSafeFile()) {
        LOG_INFO("{}", "Do Netplan Failsafe File Failed.");
        return -1;
    }

    if (!ModifyNetCfg(main, sub)) {
        LOG_INFO("{}", "Modify Network Failed.");
        return -1;
    }

    if (cosmo::util::FileExist(internal::kNetplanFailSafeFile)) {
        cosmo::util::RemoveFile(internal::kNetplanFailSafeFile);
    }

    std::string cmd_result;
    auto ret = cosmo::util::Exec(std::vector<std::string>{"netplan", "apply"}, cmd_result);
    LOG_INFO("Do [netplan apply] Get [{}] Ret:{}", cmd_result, ret);
    if (ret) {
        return -1;
    }
    return 0;
#endif
}

void DnsEffect(const std::vector<std::string>& dns_servers) {
#ifdef COSMO_NN_USE_RKNN_BACKEND
    std::string dns;
    for (const auto& server : dns_servers) {
        if (!internal::IsValidIpAddress(server)) {
            LOG_WARN("{}", "Invalid NetworkManager DNS address");
            return;
        }
        if (!dns.empty())
            dns += ",";
        dns += server;
    }
    internal::NetworkManagerConfig manager(
        [](const auto& argv, auto& out) { return cosmo::util::Exec(argv, out); });
    if (!manager.SetDns(kNetworkMainEthName, dns)) {
        LOG_WARN("NetworkManager DNS apply failed: {}", manager.Error());
    }
#elif !defined(COSMO_NN_USE_SOPHON_BACKEND)
    LOG_WARN("{}", "DNS configuration modification is not supported on x86 platform.");
    return;
#else
    std::vector<std::string> valid_dns;
    std::copy_if(dns_servers.begin(), dns_servers.end(), std::back_inserter(valid_dns),
                 [](const auto& dns) { return internal::IsValidIpAddress(dns); });

    // Atomic write: write to tmp file, then rename to avoid corruption on power loss
    const std::string tmp_path  = kResolvConfTmp;
    const std::string dest_path = kResolvConf;

    {
        std::ofstream ofile{tmp_path};
        if (!ofile) {
            LOG_WARN("DnsEffect: failed to open {}", tmp_path);
            return;
        }
        if (!valid_dns.empty()) {
            for (const auto& dns : valid_dns) {
                ofile << "nameserver " << dns << "\n";
            }
        } else {
            ofile << "\n";
        }
        ofile.flush();
    }
    // Rename is atomic on POSIX — ensures resolv.conf is never in a partial state
    if (std::rename(tmp_path.c_str(), dest_path.c_str()) != 0) {
        LOG_WARN("DnsEffect: rename {} to {} failed", tmp_path, dest_path);
    }
#endif
}

bool KillDhcp(const std::string& dev_name) {
    if (!internal::IsValidNetworkName(dev_name)) {
        LOG_WARN("KillDhcp: invalid device name: {}", dev_name);
        return false;
    }
    std::string cmd_result;
    auto ret = cosmo::util::Exec({"dhclient", "-r", dev_name}, cmd_result);
    LOG_INFO("Exec dhclient -r {} Get {} Ret:{}", dev_name, cmd_result, ret);
    return ret == 0;
}

bool IpDelFromDev(const std::string& dev_name) {
    if (!internal::IsValidNetworkName(dev_name)) {
        LOG_WARN("IpDelFromDev: invalid device name: {}", dev_name);
        return false;
    }
    std::string cmd_result;
    auto ret = cosmo::util::Exec({"ip", "route", "flush", "dev", dev_name}, cmd_result);
    LOG_INFO("Exec ip route flush dev {} Get {} Ret:{}", dev_name, cmd_result, ret);
    return ret == 0;
}

std::string IpToSubnet(const std::string& ip, int prefix_length = 24) {
    struct in_addr addr;

    if (inet_pton(AF_INET, ip.c_str(), &addr) != 1) {
        return "Invalid IP";
    }

    uint32_t mask = ~((1U << (32 - prefix_length)) - 1U);
    addr.s_addr &= htonl(mask);

    char subnet_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr, subnet_str, INET_ADDRSTRLEN);
    LOG_DEBUG("{} -> {}", ip, subnet_str);
    std::ostringstream oss;
    oss << subnet_str << "/" << prefix_length;
    return oss.str();
}

bool IpCfgToDev(const NetCardInfo& info) {
    // Validate all inputs before shell execution
    if (!internal::IsValidNetworkName(info.eth_name)) {
        LOG_WARN("IpCfgToDev: invalid device name: {}", info.eth_name);
        return false;
    }
    if (!internal::IsValidIpAddress(info.ip_addr)) {
        LOG_WARN("IpCfgToDev: invalid IP address: {}", info.ip_addr);
        return false;
    }
    if (!internal::IsValidSubnetMask(info.net_mask)) {
        LOG_WARN("IpCfgToDev: invalid subnet mask: {}", info.net_mask);
        return false;
    }

    std::string cmd_result;
    auto cmd_ret = cosmo::util::Exec(
        {"ifconfig", info.eth_name, info.ip_addr, "netmask", info.net_mask, "up"}, cmd_result);
    LOG_INFO("Exec ifconfig {} {} netmask {} up Get {} Ret:{}", info.eth_name, info.ip_addr, info.net_mask,
             cmd_result, cmd_ret);
    if (cmd_ret != 0 || !cmd_result.empty()) {
        return false;
    }

    if (!info.gateway.empty()) {
        if (!internal::IsValidIpAddress(info.gateway)) {
            LOG_WARN("IpCfgToDev: invalid gateway: {}", info.gateway);
            return false;
        }
        std::vector<std::string> route_argv;
        if (info.is_main) {
            route_argv = {"ip",         "route", "add",         "default", "via",
                          info.gateway, "dev",   info.eth_name, "metric",  "100"};
            if (!internal::IsSameIpv4Subnet(info.ip_addr, info.gateway, info.net_mask)) {
                route_argv.push_back("onlink");
            }
        } else {
            route_argv = {"ip", "route", "add", IpToSubnet(info.gateway, 24), "dev", info.eth_name};
        }

        std::string cmd_result_route;
        cmd_ret = cosmo::util::Exec(route_argv, cmd_result_route);
        if (cmd_ret != 0 || !cmd_result_route.empty()) {
            LOG_INFO("Exec ip route add Get {} Ret:{}", cmd_result_route, cmd_ret);
            return false;
        }
        LOG_INFO("Exec ip route add Get {} Ret:{}", cmd_result_route, cmd_ret);
    }

    return true;
}

bool DhcpCfgToDev(const std::string& dev_name) {
    if (!internal::IsValidNetworkName(dev_name)) {
        LOG_WARN("DhcpCfgToDev: invalid device name: {}", dev_name);
        return false;
    }
    std::string cmd_result;
    auto ret = cosmo::util::Exec({"dhclient", "-v", dev_name}, cmd_result);
    LOG_INFO("Exec dhclient -v {} Get {} Ret:{}", dev_name, cmd_result, ret);
    return ret == 0;
}

NetCardOpResult NetCardEffect(const NetCardInfo& info) {
    KillDhcp(info.eth_name);

    IpDelFromDev(info.eth_name);

    // Static IP
    if (!info.dhcp) {
        IpCfgToDev(info);
    } else {
        DhcpCfgToDev(info.eth_name);
    }

    return NetCardOpResult{cosmo::util::ErrorEnum::Success, "", ""};
}

bool GetNetProtoShellFile(const std::string& eth_name) {
#ifdef COSMO_NN_USE_RKNN_BACKEND
    internal::NetworkManagerConfig manager(
        [](const auto& argv, auto& out) { return cosmo::util::Exec(argv, out); });
    return manager.IsDhcp(eth_name);
#else
    std::ifstream file(kDhclientLeases);
    if (file.is_open()) {
        std::string line;
        while (getline(file, line)) {
            if (std::string::npos != line.find(eth_name)) {
                return true;
            }
        }
    }
    return false;
#endif
}

std::vector<NetCardInfo> GetIpInfos() {
    ifaddrs* raw_addrs{nullptr};
    if (getifaddrs(&raw_addrs) != 0) {
        return {};
    }
    // RAII wrapper to ensure freeifaddrs is always called (P0 #7)
    auto addrs = std::unique_ptr<ifaddrs, decltype(&freeifaddrs)>(raw_addrs, freeifaddrs);

    std::vector<std::string> route;
    if (std::ifstream ifile{kProcNetRoute}) {
        std::string line;
        while (getline(ifile, line)) {
            route.push_back(std::move(line));
        }
    }

    std::vector<NetCardInfo> ip_info;
    for (auto addr = addrs.get(); addr; addr = addr->ifa_next) {
        if (!addr->ifa_addr || !addr->ifa_netmask || addr->ifa_addr->sa_family != AF_INET) {
            continue;
        }

        NetCardInfo info{};

        info.eth_name = addr->ifa_name;
        char address_buffer[INET_ADDRSTRLEN];
        info.ip_addr = inet_ntop(AF_INET, &reinterpret_cast<struct sockaddr_in*>(addr->ifa_addr)->sin_addr,
                                 address_buffer, INET_ADDRSTRLEN);
        info.net_mask =
            inet_ntop(AF_INET, &reinterpret_cast<struct sockaddr_in*>(addr->ifa_netmask)->sin_addr,
                      address_buffer, INET_ADDRSTRLEN);
        if (info.eth_name == kNetworkMainEthName) {
            info.is_main = true;
        } else if (info.eth_name == kNetworkSubEthName) {
            info.is_main = false;
        } else {
            continue;
        }

        std::string name;
        uint32_t dest{0};
        uint32_t gateway{0};
        for (const auto& line : route) {
            std::istringstream iss(line);
            iss >> name >> std::hex >> dest >> gateway;
            if (name == info.eth_name && dest == 0x00000000u) {
                in_addr addrin{};
                addrin.s_addr = gateway;
                inet_ntop(AF_INET, &addrin, address_buffer, INET_ADDRSTRLEN);
                info.gateway = address_buffer;
                LOG_INFO("{} {} gateway:{}", info.eth_name, info.ip_addr, info.gateway);
                break;
            }
        }
        info.dhcp = GetNetProtoShellFile(info.eth_name);
        LOG_INFO("{} {} {}", info.eth_name, info.ip_addr, info.dhcp ? "DHCP" : "STATIC");
        ip_info.push_back(std::move(info));
    }

    return ip_info;
}

NetCardInfo GetIpInfo(const std::string& eth_name) {
    auto net_cards = GetIpInfos();
    auto it        = std::find_if(net_cards.begin(), net_cards.end(),
                                  [&](const auto& net_card) { return net_card.eth_name == eth_name; });
    if (it != net_cards.end()) {
        return *it;
    }

    return {};
}

std::string GetIPString(uint32_t ip_value) {
    char buffer[INET_ADDRSTRLEN];
    in_addr addrin;
    addrin.s_addr = ip_value;
    return inet_ntop(AF_INET, &addrin, buffer, INET_ADDRSTRLEN);
}

}  // namespace cosmo::platform
