// HardwareQueryUtil — Static utility for querying hardware and OS-level system information.

#include "service/system/impl/HardwareQueryUtil.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <sys/vfs.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <regex>
#include <sstream>
#include <thread>

#include "pcap/pcap.h"
#include "service/system/dto/SystemMsgTypes.h"
#include "service/system/impl/AcceleratorMetricsProvider.h"
#include "util/Exec.h"
#include "util/FileUtil.h"
#include "util/Log.h"
#include "util/NnBackendConstants.h"
#include "util/PathUtil.h"
#include "util/TimingConstants.h"

namespace cosmo::service::detail {

namespace {

    AcceleratorMetricsProvider& GetAcceleratorMetricsProvider() {
        static auto provider = CreateAcceleratorMetricsProvider();
        return *provider;
    }

#if defined(COSMO_NN_USE_RKNN_BACKEND)
    std::string ReadDeviceTreeText(const std::string& path, char separator = ' ') {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            return {};
        std::string value((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        std::replace(value.begin(), value.end(), '\0', separator);
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
            value.pop_back();
        const auto first = value.find_first_not_of(" \t\r\n");
        return first == std::string::npos ? std::string{} : value.substr(first);
    }
#endif

}  // namespace

// ============================================================
//  String parsing helpers
// ============================================================

bool HardwareQueryUtil::AnalysisLine(const std::string& line, const std::string& key, std::string* value,
                                     const std::string& equal_value) {
    size_t pos = line.find(key);
    if (pos != std::string::npos) {
        size_t equal_pos = line.find(equal_value);
        if (equal_pos != std::string::npos) {
            auto content =
                line.substr(equal_pos + 1).erase(0, line.substr(equal_pos + 1).find_first_not_of(' '));
            if (!content.empty() && value) {
                *value = content;
            }
        }
        return true;
    }
    return false;
}

std::string HardwareQueryUtil::KeepAlphaNum(const std::string& input) {
    std::regex pattern("[^a-zA-Z0-9]");
    return std::regex_replace(input, pattern, "");
}

std::string HardwareQueryUtil::ExtractSpecContent(const std::string& input) {
    std::regex pattern("SMP.*?(?=aarch64 aarch64 aarch64 GNU/Linux)");
    std::smatch match;
    if (std::regex_search(input, match, pattern)) {
        return match.str(0);
    }
    return "";
}

// ============================================================
//  Device static info
// ============================================================

void HardwareQueryUtil::ReadDeviceSnAndModel(std::string* device_sn, std::string* device_model) {
    if (!device_sn || !device_model) {
        return;
    }
#if defined(COSMO_NN_USE_RKNN_BACKEND)
    *device_sn = ReadDeviceTreeText("/proc/device-tree/serial-number");
    if (device_sn->empty())
        *device_sn = ReadDeviceTreeText("/sys/firmware/devicetree/base/serial-number");
    if (device_sn->empty())
        *device_sn = QueryPrimaryMac();
    *device_model = ReadDeviceTreeText("/proc/device-tree/model");
    if (device_model->empty())
        *device_model = cosmo::util::kEngineType;
    LOG_INFO("RKNN deviceSn:{} deviceModel:{}", *device_sn, *device_model);
    return;
#elif !defined(COSMO_NN_USE_SOPHON_BACKEND)
    *device_sn    = "CA16T01-X86-TRIAL";
    *device_model = "X86-TRIAL";
    LOG_INFO("x86 trial mode - mocked deviceSn:{}", *device_sn);
    LOG_INFO("x86 trial mode - mocked deviceModel:{}", *device_model);
    return;
#else
    std::string sn;
    std::string model;
    int index = 30;
    while (index-- > 0) {
        std::ifstream file("/factory/OEMconfig.ini");
        if (file.is_open()) {
            std::string line;
            while (getline(file, line)) {
                AnalysisLine(line, "DEVICE_SN", &sn);
                AnalysisLine(line, "MODULE_TYPE ", &model);
            }
            file.close();
        }
        if (sn.size() >= 5) {
            break;
        }
        std::this_thread::sleep_for(timing::kOneSecondInterval);
        LOG_INFO("{}", "Cant Get Device SN");
    }
    *device_sn = sn;
    if (!model.empty()) {
        *device_model = model;
    }
    LOG_INFO("deviceSn:{}", *device_sn);
    LOG_INFO("deviceModel:{}", *device_model);
#endif
}

std::string HardwareQueryUtil::ReadHardwareSpec() {
#if defined(COSMO_NN_USE_RKNN_BACKEND)
    auto compatible = ReadDeviceTreeText("/proc/device-tree/compatible", ',');
    return compatible.empty() ? cosmo::util::kHardwareSpecFallback : compatible;
#elif !defined(COSMO_NN_USE_SOPHON_BACKEND)
    return "X86_HARDWARE_SPEC";
#else
    auto hardware_info =
        cosmo::util::ReadFile("/sys/devices/platform/29300000.cv-emmc/mmc_host/mmc0/mmc0:0001/cid");
    if (hardware_info.empty()) {
        LOG_WARN("{}", "Cant Get Hw SN");
        return "";
    }
    auto spec = KeepAlphaNum(hardware_info);
    LOG_INFO("hwSpec:{}  hardwareInfo.size():{}", spec, hardware_info.size());
    return spec;
#endif
}

std::string HardwareQueryUtil::ReadKernelRevision() {
#if defined(COSMO_NN_USE_RKNN_BACKEND)
    struct utsname system_info {};
    if (uname(&system_info) == 0)
        return system_info.release;
    LOG_WARN("uname failed while querying RKNN kernel revision: {}", std::strerror(errno));
    return {};
#elif !defined(COSMO_NN_USE_SOPHON_BACKEND)
    return "X86-Generic-Kernel";
#else
    LOG_INFO("{}", "bm_version");
    std::string out;
    cosmo::util::Exec(std::vector<std::string>{"bm_version"}, out);
    std::istringstream stream(out);
    std::string line;
    while (std::getline(stream, line)) {
        std::string version_line;
        if (AnalysisLine(line, "KernelVersion", &version_line, ":")) {
            auto revision = ExtractSpecContent(version_line);
            LOG_INFO("hwRevision:{}", revision);
            return revision;
        }
    }
    LOG_WARN("{}", "Failed to parse KernelVersion from bm_version output");
    return "";
#endif
}

// ============================================================
//  Hardware resource utilization
// ============================================================

double HardwareQueryUtil::QueryCpuUtilization() {
    static std::mutex cpu_state_mutex;
    static uint64_t last_total = 0;
    static uint64_t last_idle  = 0;
    std::lock_guard<std::mutex> lock(cpu_state_mutex);

    std::ifstream file("/proc/stat");
    if (file.is_open()) {
        std::string line;
        if (std::getline(file, line) && line.rfind("cpu ", 0) == 0) {
            uint64_t values[10]{};
            std::istringstream input(line.substr(4));
            for (auto& value : values) {
                if (!(input >> value)) {
                    break;
                }
            }

            uint64_t total = 0;
            for (const auto value : values) {
                if (value > std::numeric_limits<uint64_t>::max() - total) {
                    LOG_WARN("{}", "CPU counters overflowed");
                    last_total = 0;
                    last_idle  = 0;
                    return 0.0;
                }
                total += value;
            }
            const uint64_t idle = values[3] + values[4];
            if (total <= last_total || idle < last_idle) {
                last_total = total;
                last_idle  = idle;
                return 0.0;
            }

            const auto total_delta = total - last_total;
            const auto idle_delta  = std::min(idle - last_idle, total_delta);
            last_total             = total;
            last_idle              = idle;
            return std::clamp(1.0 - static_cast<double>(idle_delta) / static_cast<double>(total_delta), 0.0,
                              1.0);
        }
    }
    LOG_ERRO("{}", "Failed to open /proc/stat !");
    return 0.0;
}

float HardwareQueryUtil::QueryCpuTemperature() {
    std::ifstream file("/sys/class/thermal/thermal_zone0/temp");
    if (file.is_open()) {
        int64_t temperature_milli_celsius = 0;
        if (file >> temperature_milli_celsius && temperature_milli_celsius >= -100000 &&
            temperature_milli_celsius <= 250000) {
            return static_cast<float>(temperature_milli_celsius) / 1000.0f;
        }
        LOG_WARN("{}", "Invalid CPU temperature value");
        return -1.0f;
    }
#ifdef COSMO_NN_USE_SOPHON_BACKEND
    LOG_ERRO("{}", "Failed to open /sys/class/thermal/thermal_zone0/temp !");
#endif
    return -1.0f;
}

cosmo::MsgGpuInfo HardwareQueryUtil::QueryGpuUtilization() {
    return GetAcceleratorMetricsProvider().QueryUtilization();
}

cosmo::MsgMemoryInfo HardwareQueryUtil::QueryMemoryUtilization() {
    cosmo::MsgMemoryInfo res;
    std::ifstream file("/proc/meminfo");
    if (file.is_open()) {
        std::string line;
        std::string total_prefix{"MemTotal:"};
        std::string available_prefix{"MemAvailable:"};
        while (getline(file, line)) {
            size_t inxt = line.find(total_prefix);
            size_t inxa = line.find(available_prefix);
            if (inxt != std::string::npos) {
                std::istringstream iss(line.substr(inxt + total_prefix.size()));
                uint64_t value_kib = 0;
                if (iss >> value_kib &&
                    value_kib <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) / 1024) {
                    res.memtotal = static_cast<int64_t>(value_kib * 1024);
                }
            } else if (inxa != std::string::npos) {
                std::istringstream iss(line.substr(inxa + available_prefix.size()));
                uint64_t value_kib = 0;
                if (iss >> value_kib &&
                    value_kib <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) / 1024) {
                    res.memavailable = static_cast<int64_t>(value_kib * 1024);
                }
            }
        }
        return res;
    }
    LOG_ERRO("{}", "Failed to open /proc/meminfo !");
    return res;
}

cosmo::MsgDiskInfo HardwareQueryUtil::QueryDiskUtilization() {
    cosmo::MsgDiskInfo res;
    struct statfs disk_info {};
    std::string disk_path = cosmo::path::GetBaseDir();
    if (disk_path.empty()) {
        char current_absolute_path[PATH_MAX] = {};
        int cnt                              = readlink("/proc/self/exe", current_absolute_path, PATH_MAX);
        if (cnt < 0 || cnt >= PATH_MAX) {
            LOG_ERRO("{}", "Failed to open /proc/self/exe !");
            return res;
        }
        current_absolute_path[cnt] = '\0';
        for (int i = cnt - 1; i >= 0; --i) {
            if (current_absolute_path[i] == '/') {
                current_absolute_path[i + 1] = '\0';
                break;
            }
        }
        std::string path = current_absolute_path;
        auto pos         = path.find("bin");
        if (pos != std::string::npos) {
            path = path.substr(0, pos + strlen("bin"));
        }
        disk_path = path;
    }
    if (statfs(disk_path.c_str(), &disk_info) == 0) {
        if (disk_info.f_bsize <= 0 ||
            static_cast<uint64_t>(disk_info.f_blocks) >
                static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) /
                    static_cast<uint64_t>(disk_info.f_bsize) ||
            static_cast<uint64_t>(disk_info.f_bavail) >
                static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) /
                    static_cast<uint64_t>(disk_info.f_bsize)) {
            LOG_ERRO("Invalid disk size reported for path [{}]", disk_path);
            return res;
        }
        res.disktotal     = static_cast<int64_t>(static_cast<uint64_t>(disk_info.f_bsize) *
                                                 static_cast<uint64_t>(disk_info.f_blocks));
        res.diskavailable = static_cast<int64_t>(static_cast<uint64_t>(disk_info.f_bsize) *
                                                 static_cast<uint64_t>(disk_info.f_bavail));
        return res;
    }
    LOG_ERRO("Failed to read disk path[{}] error[{}]!", disk_path, strerror(errno));
    return res;
}

int64_t HardwareQueryUtil::QueryAvailableGpuMemoryMB() {
    return GetAcceleratorMetricsProvider().QueryAvailableMemoryMB();
}

// ============================================================
//  Network interface queries
// ============================================================

std::string HardwareQueryUtil::QueryPrimaryMac() {
    char errbuf[PCAP_ERRBUF_SIZE] = {};
    pcap_if_t* alldevs            = nullptr;
    if (pcap_findalldevs(&alldevs, errbuf) != 0) {
        LOG_ERRO("pcap_findalldevs failed: {}", errbuf);
        return "-1";
    }
    for (pcap_if_t* d = alldevs; d != nullptr; d = d->next) {
        if (d->name == nullptr || (d->flags & PCAP_IF_LOOPBACK) != 0) {
            continue;
        }
        const std::string prefix = d->name;
        const int sock           = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            LOG_WARN("socket() failed while querying MAC for {}", prefix);
            continue;
        }
        struct ifreq request {};
        std::strncpy(request.ifr_name, prefix.c_str(), IFNAMSIZ - 1);
        if (ioctl(sock, SIOCGIFHWADDR, &request) < 0) {
            LOG_DEBUG("SIOCGIFHWADDR failed for {}", prefix);
            close(sock);
            continue;
        }
        char mac[13];
        // Preserve the deployed device-id representation for compatibility.
        snprintf(mac, sizeof(mac), "%X%X%X%X%X%X", static_cast<unsigned char>(request.ifr_hwaddr.sa_data[0]),
                 static_cast<unsigned char>(request.ifr_hwaddr.sa_data[1]),
                 static_cast<unsigned char>(request.ifr_hwaddr.sa_data[2]),
                 static_cast<unsigned char>(request.ifr_hwaddr.sa_data[3]),
                 static_cast<unsigned char>(request.ifr_hwaddr.sa_data[4]),
                 static_cast<unsigned char>(request.ifr_hwaddr.sa_data[5]));
        close(sock);
        pcap_freealldevs(alldevs);
        return mac;
    }
    pcap_freealldevs(alldevs);
    LOG_ERRO("{}", "[QueryPrimaryMac] Fail to find Net Dev!");
    return "-1";
}

std::string HardwareQueryUtil::QueryPrimaryIPv4() {
    char errbuf[PCAP_ERRBUF_SIZE] = {};
    pcap_if_t* alldevs            = nullptr;
    if (pcap_findalldevs(&alldevs, errbuf) != 0) {
        LOG_ERRO("pcap_findalldevs failed: {}", errbuf);
        return "-1";
    }
    for (pcap_if_t* d = alldevs; d != nullptr; d = d->next) {
        if (d->name == nullptr || (d->flags & PCAP_IF_LOOPBACK) != 0) {
            continue;
        }
        const std::string prefix = d->name;
        const int sock           = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            LOG_WARN("socket() failed while querying IPv4 for {}", prefix);
            continue;
        }
        struct ifreq request {};
        std::strncpy(request.ifr_name, prefix.c_str(), IFNAMSIZ - 1);
        if (ioctl(sock, SIOCGIFADDR, &request) < 0) {
            LOG_DEBUG("SIOCGIFADDR failed for {}", prefix);
            close(sock);
            continue;
        }
        const auto* address      = reinterpret_cast<const struct sockaddr_in*>(&(request.ifr_addr));
        char ip[INET_ADDRSTRLEN] = {};
        const bool converted     = inet_ntop(AF_INET, &address->sin_addr, ip, sizeof(ip)) != nullptr;
        close(sock);
        if (!converted) {
            continue;
        }
        pcap_freealldevs(alldevs);
        return ip;
    }
    LOG_ERRO("{}", "[QueryPrimaryIPv4] Fail to find Net Dev!");
    pcap_freealldevs(alldevs);
    return "-1";
}

std::vector<std::pair<std::string, std::string>> HardwareQueryUtil::QueryAllMacs() {
    std::vector<std::pair<std::string, std::string>> mac_infos;
    char errbuf[PCAP_ERRBUF_SIZE] = {};
    pcap_if_t* alldevs            = nullptr;
    if (pcap_findalldevs(&alldevs, errbuf) != 0) {
        LOG_ERRO("pcap_findalldevs failed: {}", errbuf);
        return mac_infos;
    }
    for (pcap_if_t* d = alldevs; d != nullptr; d = d->next) {
        if (d->name == nullptr || (d->flags & PCAP_IF_LOOPBACK) != 0) {
            continue;
        }
        const std::string prefix = d->name;
        const int sock           = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            LOG_WARN("socket() failed while querying MAC for {}", prefix);
            continue;
        }
        struct ifreq request {};
        std::strncpy(request.ifr_name, prefix.c_str(), IFNAMSIZ - 1);
        if (ioctl(sock, SIOCGIFHWADDR, &request) < 0) {
            LOG_DEBUG("SIOCGIFHWADDR failed for {}", prefix);
            close(sock);
            continue;
        }
        char mac[18];
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 static_cast<unsigned char>(request.ifr_hwaddr.sa_data[0]),
                 static_cast<unsigned char>(request.ifr_hwaddr.sa_data[1]),
                 static_cast<unsigned char>(request.ifr_hwaddr.sa_data[2]),
                 static_cast<unsigned char>(request.ifr_hwaddr.sa_data[3]),
                 static_cast<unsigned char>(request.ifr_hwaddr.sa_data[4]),
                 static_cast<unsigned char>(request.ifr_hwaddr.sa_data[5]));
        LOG_INFO("{} MAC:{}", prefix, mac);
        close(sock);
        mac_infos.emplace_back(prefix, mac);
    }
    pcap_freealldevs(alldevs);
    return mac_infos;
}

}  // namespace cosmo::service::detail
