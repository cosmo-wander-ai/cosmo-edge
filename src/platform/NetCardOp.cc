// NetCardOp — Network card configuration utilities.

#include "platform/NetCardOp.h"

#include <arpa/inet.h>
#include <ifaddrs.h>

#include <filesystem>
#include <optional>
#include <sstream>
#include <vector>

#include "platform/NetCardOpInternal.h"
#include "util/Exec.h"
#include "util/FileUtil.h"
#include "util/FormatString.h"
#include "util/Log.h"

namespace cosmo::platform {

// Forward declarations — defined in NetCardOpConfig.cc
NetCardOpResult NetCardEffect(const NetCardInfo& info);

namespace internal = cosmo::platform::internal;

NetCardOpResult DoNetCard(const NetCardInfo& info) {
#ifndef COSMO_NN_USE_SOPHON_BACKEND
    LOG_WARN("{}", "Network card configuration is not supported on x86 platform.");
    return NetCardOpResult{cosmo::util::ErrorEnum::OperationNotSupport, "Not supported on x86", ""};
#else
    auto real_net = GetIpInfo(info.eth_name);
    if ((info.is_dhcp_change) || (real_net.dhcp != info.dhcp) || (real_net.ip_addr != info.ip_addr) ||
        (real_net.net_mask != info.net_mask) || (real_net.gateway != info.gateway)) {
        if (info.is_dhcp_change) {
            LOG_INFO("NetCard:{} DHCP Change To {}", real_net.eth_name, info.dhcp ? "OPEN" : "CLOSE");
        } else {
            LOG_INFO("NetCard:{} IP/Mask/Gateway Change From {}/{}/{} To {}/{}/{}", real_net.eth_name,
                     real_net.ip_addr, real_net.net_mask, real_net.gateway, info.ip_addr, info.net_mask,
                     info.gateway);
        }

        auto res = NetCardEffect(info);
        LOG_INFO("NetCardEffect done, code:{} msg:{} result:{}", static_cast<int>(res.error_code),
                 res.message, res.result_data);
        return res;
    }

    return NetCardOpResult{cosmo::util::ErrorEnum::Success, {}, {}};
#endif
}

// Convert subnet mask (e.g., "255.255.255.0") to CIDR prefix length (e.g., 24).
// Returns std::nullopt if the mask is invalid.
std::optional<int> SubnetMaskToCIDR(const std::string& subnet_mask) {
    uint32_t mask = ntohl(inet_addr(subnet_mask.c_str()));

    int cidr = 0;
    while (mask & 0x80000000) {
        cidr++;
        mask <<= 1;
    }

    // Verify contiguous 1s followed by contiguous 0s
    if (mask != 0) {
        LOG_WARN("Invalid subnet mask: {}", subnet_mask);
        return std::nullopt;
    }

    return cidr;
}

bool DoNetplanFailSafeFile() {
    if (!internal::EnsureNetplanFailSafeBak()) {
        return false;
    }

    if (!internal::CopyFileWithLog(internal::kNetplanFailSafeBak, internal::kNetplanFailSafeFile)) {
        return false;
    }

    std::string cmd = "chmod 600 " + internal::kNetplanFailSafeFile;
    std::string cmd_result;
    auto ret = cosmo::util::Exec(cmd, cmd_result);
    if (ret != 0 || !cmd_result.empty()) {
        LOG_INFO("Exec [{}] Get [{}] Ret:{}", cmd, cmd_result, ret);
        return false;
    }
    LOG_INFO("Exec [{}] Get [{}] Ret:{}", cmd, cmd_result, ret);

    internal::RemoveOldNetcfgYamlFiles();

    LOG_INFO("{}", "Executing Netplan Apply With Failsafe Config");
    cmd = "netplan apply";
    ret = cosmo::util::Exec(cmd, cmd_result);
    LOG_INFO("Do [{}] Get [{}] Ret:{}", cmd, cmd_result, ret);
    if (ret) {
        return false;
    }

    return true;
}

// YAML indentation constants for netplan config generation
namespace {
    constexpr const char* kIndent2  = "  ";
    constexpr const char* kIndent4  = "    ";
    constexpr const char* kIndent6  = "      ";
    constexpr const char* kIndent8  = "        ";
    constexpr const char* kIndent10 = "          ";
}  // namespace

bool ModifyNetCfg(const NetCardInfo& main, const NetCardInfo& sub) {
    std::string netplan_info;
    netplan_info.append("network:\n");
    netplan_info.append(kIndent2).append("version: 2\n");
    netplan_info.append(kIndent2).append("renderer: networkd\n");
    netplan_info.append(kIndent2).append("ethernets:\n");

    // eth0
    netplan_info.append(kIndent4).append(main.eth_name).append(":\n");
    if (main.dhcp) {
        netplan_info.append(kIndent6).append("dhcp4: yes\n");
        netplan_info.append(kIndent6).append("addresses: []\n");
        netplan_info.append(kIndent6).append("optional: yes\n");
        netplan_info.append(kIndent6).append("dhcp-identifier: mac\n");
    } else {
        auto cidr = SubnetMaskToCIDR(main.net_mask);
        if (!cidr) {
            LOG_WARN("Invalid main subnet mask: {}", main.net_mask);
            return false;
        }
        netplan_info.append(kIndent6).append("dhcp4: no\n");
        netplan_info.append(kIndent6)
            .append("addresses: [")
            .append(main.ip_addr)
            .append("/")
            .append(std::to_string(*cidr))
            .append("]\n");
        if (!main.gateway.empty()) {
            netplan_info.append(kIndent6).append("routes:\n");
            netplan_info.append(kIndent8).append("- to: default\n");
            netplan_info.append(kIndent10).append("via: ").append(main.gateway).append("\n");
            if (!internal::IsSameIpv4Subnet(main.ip_addr, main.gateway, main.net_mask)) {
                netplan_info.append(kIndent10).append("on-link: true\n");
            }
        }
        netplan_info.append(kIndent6).append("optional: yes\n");

        if (!main.dnss.empty()) {
            netplan_info.append(kIndent6).append("nameservers:\n");
            netplan_info.append(kIndent8).append("addresses: [");
            for (size_t i = 0; i < main.dnss.size(); i++) {
                if (i > 0) {
                    netplan_info.append(", ");
                }
                netplan_info.append(main.dnss[i]);
            }
            netplan_info.append("]\n");
        }
    }

    // eth1
    netplan_info.append(kIndent4).append(sub.eth_name).append(":\n");
    if (sub.dhcp) {
        netplan_info.append(kIndent6).append("dhcp4: yes\n");
        netplan_info.append(kIndent6).append("addresses: []\n");
        netplan_info.append(kIndent6).append("optional: yes\n");
        netplan_info.append(kIndent6).append("dhcp-identifier: mac\n");
    } else {
        auto cidr = SubnetMaskToCIDR(sub.net_mask);
        if (!cidr) {
            LOG_WARN("Invalid sub subnet mask: {}", sub.net_mask);
            return false;
        }
        netplan_info.append(kIndent6).append("dhcp4: no\n");
        netplan_info.append(kIndent6)
            .append("addresses: [")
            .append(sub.ip_addr)
            .append("/")
            .append(std::to_string(*cidr))
            .append("]\n");
        netplan_info.append(kIndent6).append("optional: yes\n");
    }

    if (!cosmo::util::WriteFile(internal::kNetplanConfigFile, netplan_info)) {
        LOG_WARN("Write {} With {} Failed", internal::kNetplanConfigFile, netplan_info);
        return false;
    }
    std::string cmd = "chmod 600 " + internal::kNetplanConfigFile;
    std::string cmd_result;
    auto ret = cosmo::util::Exec(cmd, cmd_result);
    LOG_INFO("Exec [{}] Get [{}] Ret:{}", cmd, cmd_result, ret);
    return true;
}

}  // namespace cosmo::platform

#include <nlohmann/json.hpp>

#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo::platform {
void to_json(nlohmann::json& j, const NetCardInfo& v) {
    j["dhcp"]    = v.dhcp;
    j["ethName"] = v.eth_name;
    j["ipAddr"]  = v.ip_addr;
    j["netMask"] = v.net_mask;
    j["gateway"] = v.gateway;
}

void from_json(const nlohmann::json& j, NetCardInfo& v) {
    if (j.contains("dhcp") && !j["dhcp"].is_null())
        j.at("dhcp").get_to(v.dhcp);
    if (j.contains("ethName") && !j["ethName"].is_null())
        j.at("ethName").get_to(v.eth_name);
    if (j.contains("ipAddr") && !j["ipAddr"].is_null())
        j.at("ipAddr").get_to(v.ip_addr);
    if (j.contains("netMask") && !j["netMask"].is_null())
        j.at("netMask").get_to(v.net_mask);
    if (j.contains("gateway") && !j["gateway"].is_null())
        j.at("gateway").get_to(v.gateway);
}

}  // namespace cosmo::platform
