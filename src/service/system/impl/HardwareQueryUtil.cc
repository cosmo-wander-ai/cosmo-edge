// HardwareQueryUtil — Static utility for querying hardware and OS-level system information.

#include "service/system/impl/HardwareQueryUtil.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/vfs.h>
#include <unistd.h>

#include <climits>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <regex>
#include <sstream>
#include <thread>

#ifdef COSMO_NN_USE_SOPHON_BACKEND
#include "bmlib_runtime.h"
#endif
#include "pcap/pcap.h"
#include "service/detail/ServiceRegistry.h"
#include "service/system/dto/SystemMsgTypes.h"
#include "util/Exec.h"
#include "util/FileUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/TimingConstants.h"

namespace cosmo::service::detail {

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
#ifndef COSMO_NN_USE_SOPHON_BACKEND
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
#ifndef COSMO_NN_USE_SOPHON_BACKEND
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
#ifndef COSMO_NN_USE_SOPHON_BACKEND
    return "X86-Generic-Kernel";
#else
    LOG_INFO("{}", "bm_version");
    std::string out;
    cosmo::util::Exec("bm_version", out);
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
    std::ifstream file("/proc/stat");
    if (file.is_open()) {
        static int64_t last_total = 0, last_idle = 0;
        std::string line;
        std::string prefix{"cpu "};
        while (getline(file, line)) {
            size_t inx = line.find(prefix);
            if (inx != std::string::npos) {
                int64_t values[10]{};
                std::istringstream iss(line.substr(inx + prefix.size()));
                iss >> values[0] >> values[1] >> values[2] >> values[3] >> values[4] >> values[5] >>
                    values[6] >> values[7] >> values[8] >> values[9];
                int64_t total = std::accumulate(std::begin(values), std::end(values), 0LL), idle = values[3];
                if (total == last_total) {
                    last_total = total;
                    last_idle  = idle;
                    file.close();
                    return 0.0;
                }
                double ratio = static_cast<double>(idle - last_idle) / (total - last_total);
                ratio        = 1 - ratio;
                last_total   = total;
                last_idle    = idle;
                std::stringstream ss;
                ss << std::fixed << std::setprecision(4) << ratio;
                ss >> ratio;
                file.close();
                return ratio;
            }
        }
    }
    file.close();
    LOG_ERRO("{}", "Failed to open /proc/stat !");
    return {-1};
}

float HardwareQueryUtil::QueryCpuTemperature() {
    std::ifstream file("/sys/class/thermal/thermal_zone0/temp");
    if (file.is_open()) {
        std::string temp_str;
        std::getline(file, temp_str);
        return std::stof(temp_str) / 1000.0f;
    }
    file.close();
#ifdef COSMO_NN_USE_SOPHON_BACKEND
    LOG_ERRO("{}", "Failed to open /sys/class/thermal/thermal_zone0/temp !");
#endif
    return {-1};
}

#ifdef COSMO_NN_USE_SOPHON_BACKEND
cosmo::MsgGpuInfo HardwareQueryUtil::QueryGpuUtilization() {
    cosmo::MsgGpuInfo res;
    bm_handle_t handle;
    auto ret = bm_dev_request(&handle, 0);
    if (ret != BM_SUCCESS) {
        LOG_ERRO("bm_dev_request failed:{}", ret);
        return res;
    }
    bm_dev_stat stat;
    ret = bm_get_stat(handle, &stat);
    if (ret != BM_SUCCESS) {
        LOG_ERRO("bm_get_stat failed:{}", ret);
        bm_dev_free(handle);
        return res;
    }
    res.gpumemtotal     = stat.mem_total;
    res.gpumemusage     = stat.mem_used * 1.0 / stat.mem_total;
    res.gpuusage        = stat.tpu_util * 0.01;
    res.gpumemavailable = 0;
    for (int i = 0; i < stat.heap_num; i++) {
        res.gpumemavailable += stat.heap_stat[i].mem_avail;
        cosmo::MsgGpuDevUsage dev_usage;
        dev_usage.gpumemtotal     = stat.heap_stat[i].mem_total;
        dev_usage.gpumemavailable = stat.heap_stat[i].mem_avail;
        if ((dev_usage.gpumemtotal > dev_usage.gpumemavailable) && (dev_usage.gpumemavailable > 0)) {
            dev_usage.gpumemusage = static_cast<double>(dev_usage.gpumemtotal - dev_usage.gpumemavailable) /
                                    dev_usage.gpumemtotal;
        }
        res.gpudevusage.push_back(dev_usage);
        LOG_INFO("Heap:{} Total:{}MB AVAIBLE:{}MB Used:{}MB", i, stat.heap_stat[i].mem_total,
                 stat.heap_stat[i].mem_avail, stat.heap_stat[i].mem_used);
    }
    LOG_INFO("[GPU MEM:{}MB AVAIBLE:{}MB MemUsage:{} TpuUsage:{}]  stat.heap_num:{}", res.gpumemtotal,
             res.gpumemavailable, res.gpumemusage, res.gpuusage, stat.heap_num);
    bm_dev_free(handle);
    return res;
}
#else
cosmo::MsgGpuInfo HardwareQueryUtil::QueryGpuUtilization() {
    // CPU backend — no GPU/TPU hardware
    return cosmo::MsgGpuInfo{};
}
#endif  // COSMO_NN_USE_SOPHON_BACKEND

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
                iss >> res.memtotal;
                res.memtotal = res.memtotal << 10;
            } else if (inxa != std::string::npos) {
                std::istringstream iss(line.substr(inxa + available_prefix.size()));
                iss >> res.memavailable;
                res.memavailable = res.memavailable << 10;
            }
        }
        file.close();
        return res;
    }
    file.close();
    LOG_ERRO("{}", "Failed to open /proc/meminfo !");
    return res;
}

cosmo::MsgDiskInfo HardwareQueryUtil::QueryDiskUtilization() {
    cosmo::MsgDiskInfo res;
    struct statfs disk_info;
    static std::string cached_path;
    if (cached_path.empty()) {
        cached_path = cosmo::path::GetBaseDir();
    }
    if (cached_path.empty()) {
        char current_absolute_path[PATH_MAX];
        int cnt = readlink("/proc/self/exe", current_absolute_path, PATH_MAX);
        if (cnt < 0 || cnt >= PATH_MAX) {
            LOG_ERRO("{}", "Failed to open /proc/self/exe !");
            return res;
        }
        for (int i = cnt; i >= 0; --i) {
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
        cached_path = path;
    }
    if (statfs(cached_path.c_str(), &disk_info) == 0) {
        int64_t disk_total_size = static_cast<int64_t>(disk_info.f_bsize) * disk_info.f_blocks;
        int64_t disk_available  = static_cast<int64_t>(disk_info.f_bsize) * disk_info.f_bavail;
        res.diskavailable       = disk_available;
        res.disktotal           = disk_total_size;
        return res;
    }
    LOG_ERRO("Failed to read disk path[{}] error[{}]!", cached_path, strerror(errno));
    return res;
}

#ifdef COSMO_NN_USE_SOPHON_BACKEND
int64_t HardwareQueryUtil::QueryAvailableGpuMemoryMB() {
    bm_handle_t handle;
    auto ret = bm_dev_request(&handle, 0);
    if (ret != BM_SUCCESS) {
        LOG_ERRO("GetAvailableMemoryMB: bm_dev_request failed:{}", ret);
        return -1;
    }
    bm_dev_stat stat;
    ret = bm_get_stat(handle, &stat);
    if (ret != BM_SUCCESS) {
        LOG_ERRO("GetAvailableMemoryMB: bm_get_stat failed:{}", ret);
        bm_dev_free(handle);
        return -1;
    }
    int64_t total_avail_mb = 0;
    for (int i = 0; i < stat.heap_num; i++) {
        total_avail_mb += stat.heap_stat[i].mem_avail;
    }
    bm_dev_free(handle);
    LOG_INFO("[GPU mem check] available:{}MB total:{}MB", total_avail_mb, stat.mem_total);
    return total_avail_mb;
}
#else
int64_t HardwareQueryUtil::QueryAvailableGpuMemoryMB() {
    // CPU backend — no GPU/TPU hardware
    return 0;
}
#endif  // COSMO_NN_USE_SOPHON_BACKEND

// ============================================================
//  Network interface queries
// ============================================================

std::string HardwareQueryUtil::QueryPrimaryMac() {
    struct ifreq ifreq;
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_if_t* alldevs = nullptr;
    pcap_findalldevs(&alldevs, errbuf);
    for (pcap_if_t* d = alldevs; d != nullptr; d = d->next) {
        std::string prefix = d->name;
        int sock;
        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            perror("socket");
            continue;
        }
        strncpy(ifreq.ifr_name, prefix.c_str(), IFNAMSIZ - 1);
        ifreq.ifr_name[IFNAMSIZ - 1] = '\0';
        if (ioctl(sock, SIOCGIFHWADDR, &ifreq) < 0) {
            perror("ioctl");
            close(sock);
            continue;
        }
        char mac[13];
        snprintf(mac, 13, "%X%X%X%X%X%X", static_cast<unsigned char>(ifreq.ifr_hwaddr.sa_data[0]),
                 static_cast<unsigned char>(ifreq.ifr_hwaddr.sa_data[1]),
                 static_cast<unsigned char>(ifreq.ifr_hwaddr.sa_data[2]),
                 static_cast<unsigned char>(ifreq.ifr_hwaddr.sa_data[3]),
                 static_cast<unsigned char>(ifreq.ifr_hwaddr.sa_data[4]),
                 static_cast<unsigned char>(ifreq.ifr_hwaddr.sa_data[5]));
        close(sock);
        pcap_freealldevs(alldevs);
        return mac;
    }
    pcap_freealldevs(alldevs);
    LOG_ERRO("{}", "[QueryPrimaryMac] Fail to find Net Dev!");
    return "-1";
}

std::string HardwareQueryUtil::QueryPrimaryIPv4() {
    struct ifreq ifreq;
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_if_t* alldevs = nullptr;
    pcap_findalldevs(&alldevs, errbuf);
    for (pcap_if_t* d = alldevs; d != nullptr; d = d->next) {
        std::string prefix = d->name;
        int sock;
        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            perror("socket");
            continue;
        }
        strncpy(ifreq.ifr_name, prefix.c_str(), IFNAMSIZ - 1);
        ifreq.ifr_name[IFNAMSIZ - 1] = '\0';
        if (ioctl(sock, SIOCGIFADDR, &ifreq) < 0) {
            perror("ioctl");
            close(sock);
            continue;
        }
        auto* sin = reinterpret_cast<struct sockaddr_in*>(&(ifreq.ifr_addr));
        close(sock);
        pcap_freealldevs(alldevs);
        return inet_ntoa(sin->sin_addr);
    }
    LOG_ERRO("{}", "[QueryPrimaryIPv4] Fail to find Net Dev!");
    pcap_freealldevs(alldevs);
    return "-1";
}

std::vector<std::pair<std::string, std::string>> HardwareQueryUtil::QueryAllMacs() {
    std::vector<std::pair<std::string, std::string>> mac_infos;
    struct ifreq ifreq;
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_if_t* alldevs = nullptr;
    pcap_findalldevs(&alldevs, errbuf);
    for (pcap_if_t* d = alldevs; d != nullptr; d = d->next) {
        std::string prefix = d->name;
        int sock;
        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            perror("socket");
            continue;
        }
        strncpy(ifreq.ifr_name, prefix.c_str(), IFNAMSIZ - 1);
        ifreq.ifr_name[IFNAMSIZ - 1] = '\0';
        if (ioctl(sock, SIOCGIFHWADDR, &ifreq) < 0) {
            perror("ioctl");
            close(sock);
            continue;
        }
        char mac[18];
        snprintf(mac, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
                 static_cast<unsigned char>(ifreq.ifr_hwaddr.sa_data[0]),
                 static_cast<unsigned char>(ifreq.ifr_hwaddr.sa_data[1]),
                 static_cast<unsigned char>(ifreq.ifr_hwaddr.sa_data[2]),
                 static_cast<unsigned char>(ifreq.ifr_hwaddr.sa_data[3]),
                 static_cast<unsigned char>(ifreq.ifr_hwaddr.sa_data[4]),
                 static_cast<unsigned char>(ifreq.ifr_hwaddr.sa_data[5]));
        LOG_INFO("{} MAC:{}", prefix, mac);
        close(sock);
        std::string mac_addr = mac;
        mac_infos.push_back(std::make_pair(prefix, mac_addr));
    }
    pcap_freealldevs(alldevs);
    return mac_infos;
}

}  // namespace cosmo::service::detail
