// Internal utilities shared between NetCardOp.cc and NetCardOpConfig.cc.
// NOT part of the public API — do not include from outside platform/.
#pragma once

#include <arpa/inet.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

#include "platform/NetCardOp.h"
#include "platform/PlatformConstants.h"
#include "util/Exec.h"
#include "util/FileUtil.h"
#include "util/Log.h"

namespace cosmo::platform::internal {

// ---- Path constants ----
using cosmo::platform::kNetplanConfigFile;
using cosmo::platform::kNetplanDir;
using cosmo::platform::kNetplanFailSafeBak;
using cosmo::platform::kNetplanFailSafeFile;
using cosmo::platform::kPackagedFailSafeBak;

// ---- Input validation (P0 #1 — command injection prevention) ----

// Validate network interface name: only [a-zA-Z0-9] allowed, max 15 chars.
inline bool IsValidNetworkName(const std::string& name) {
    if (name.empty() || name.size() > 15)
        return false;
    return std::all_of(name.begin(), name.end(),
                       [](char c) { return std::isalnum(static_cast<unsigned char>(c)); });
}

// Validate IP address using inet_pton (replaces hand-rolled CheckIsIpAddr).
inline bool IsValidIpAddress(const std::string& ip) {
    if (ip.empty())
        return false;
    struct in_addr addr;
    return inet_pton(AF_INET, ip.c_str(), &addr) == 1;
}

// Validate subnet mask: must be valid IP and contiguous high bits.
inline bool IsValidSubnetMask(const std::string& mask) {
    struct in_addr addr;
    if (inet_pton(AF_INET, mask.c_str(), &addr) != 1)
        return false;
    uint32_t m = ntohl(addr.s_addr);
    // A valid mask is contiguous 1s followed by contiguous 0s
    uint32_t inv = ~m;
    return m == 0 || ((inv & (inv + 1)) == 0);
}

inline bool IsSameIpv4Subnet(const std::string& ip, const std::string& gateway, const std::string& mask) {
    struct in_addr ip_addr;
    struct in_addr gateway_addr;
    struct in_addr mask_addr;
    if (inet_pton(AF_INET, ip.c_str(), &ip_addr) != 1 ||
        inet_pton(AF_INET, gateway.c_str(), &gateway_addr) != 1 ||
        inet_pton(AF_INET, mask.c_str(), &mask_addr) != 1) {
        return false;
    }

    const auto ip_value      = ntohl(ip_addr.s_addr);
    const auto gateway_value = ntohl(gateway_addr.s_addr);
    const auto mask_value    = ntohl(mask_addr.s_addr);
    return (ip_value & mask_value) == (gateway_value & mask_value);
}

// ---- Shared file utilities ----

inline bool CopyFileWithLog(const std::string& src, const std::string& dst) {
    std::error_code ec;
    std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        LOG_WARN("Copy netplan file from {} to {} failed: {}", src, dst, ec.message());
        return false;
    }
    LOG_INFO("Copy netplan file from {} to {} success", src, dst);
    return true;
}

inline bool IsUsableNetplanBackup(const std::string& path) {
    auto content = cosmo::util::ReadFile(path);
    if (content.empty()) {
        LOG_WARN("Netplan backup {} is empty or unreadable", path);
        return false;
    }
    if (content.find(kNetworkMainEthName) == std::string::npos ||
        content.find(kNetworkSubEthName) == std::string::npos) {
        LOG_WARN("Netplan backup {} does not contain {} and {}, skip it", path, kNetworkMainEthName,
                 kNetworkSubEthName);
        return false;
    }
    return true;
}

inline bool WriteDefaultFailSafeBak() {
    std::string netplanInfo;
    netplanInfo.append("network:\n");
    netplanInfo.append("  version: 2\n");
    netplanInfo.append("  renderer: networkd\n");
    netplanInfo.append("  ethernets:\n");
    netplanInfo.append("    ").append(kNetworkMainEthName).append(":\n");
    netplanInfo.append("      dhcp4: no\n");
    netplanInfo.append("      addresses: [192.168.0.18/24]\n");
    netplanInfo.append("      optional: yes\n");
    netplanInfo.append("    ").append(kNetworkSubEthName).append(":\n");
    netplanInfo.append("      dhcp4: no\n");
    netplanInfo.append("      addresses: [192.168.1.18/24]\n");
    netplanInfo.append("      optional: yes\n");

    if (!cosmo::util::WriteFile(kNetplanFailSafeBak, netplanInfo)) {
        LOG_WARN("Write default netplan failsafe backup {} failed", kNetplanFailSafeBak);
        return false;
    }

    std::string cmdResult;
    auto ret = cosmo::util::Exec("chmod 600 " + kNetplanFailSafeBak, cmdResult);
    LOG_INFO("Write default netplan failsafe backup {} Ret:{} Result:{}", kNetplanFailSafeBak, ret,
             cmdResult);
    return ret == 0;
}

inline bool HasNetcfgYamlName(const std::string& filename) {
    const std::string suffix = "-netcfg.yaml";
    return filename.size() >= suffix.size() &&
           filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline std::vector<std::string> LegacyNetcfgBackups() {
    std::vector<std::string> backups;
    std::error_code ec;
    if (!std::filesystem::is_directory(kNetplanDir, ec)) {
        return backups;
    }

    for (const auto& entry : std::filesystem::directory_iterator(kNetplanDir, ec)) {
        if (ec) {
            LOG_WARN("List {} failed: {}", kNetplanDir, ec.message());
            break;
        }
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        auto filename            = entry.path().filename().string();
        const std::string suffix = "-netcfg.yaml.bak";
        if (filename.size() >= suffix.size() &&
            filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0) {
            backups.push_back(entry.path().string());
        }
    }
    return backups;
}

inline bool EnsureNetplanFailSafeBak() {
    if (cosmo::util::FileExist(kNetplanFailSafeBak)) {
        return true;
    }

    LOG_WARN("{} not exist, try create it from packaged or legacy netplan backup", kNetplanFailSafeBak);

    if (cosmo::util::FileExist(kPackagedFailSafeBak) && IsUsableNetplanBackup(kPackagedFailSafeBak) &&
        CopyFileWithLog(kPackagedFailSafeBak, kNetplanFailSafeBak)) {
        return true;
    }

    auto backups = LegacyNetcfgBackups();
    if (std::any_of(backups.begin(), backups.end(), [](const auto& backup) {
            return IsUsableNetplanBackup(backup) && CopyFileWithLog(backup, kNetplanFailSafeBak);
        })) {
        return true;
    }

    LOG_WARN("No usable netplan failsafe backup found, create default {}", kNetplanFailSafeBak);
    return WriteDefaultFailSafeBak();
}

inline void RemoveOldNetcfgYamlFiles() {
    std::error_code ec;
    if (!std::filesystem::is_directory(kNetplanDir, ec)) {
        LOG_WARN("{} is not a directory: {}", kNetplanDir, ec.message());
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(kNetplanDir, ec)) {
        if (ec) {
            LOG_WARN("List {} failed: {}", kNetplanDir, ec.message());
            return;
        }
        if (!entry.is_regular_file(ec)) {
            continue;
        }

        const auto path     = entry.path();
        const auto filename = path.filename().string();
        if (filename == std::filesystem::path(kNetplanFailSafeFile).filename().string()) {
            continue;
        }
        if (filename == std::filesystem::path(kNetplanConfigFile).filename().string() ||
            HasNetcfgYamlName(filename)) {
            std::filesystem::remove(path, ec);
            if (ec) {
                LOG_WARN("Remove old netplan config {} failed: {}", path.string(), ec.message());
                ec.clear();
            } else {
                LOG_INFO("Remove old netplan config {}", path.string());
            }
        }
    }
}

}  // namespace cosmo::platform::internal
