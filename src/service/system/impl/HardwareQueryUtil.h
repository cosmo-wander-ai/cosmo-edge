#pragma once

#include <string>
#include <utility>
#include <vector>

#include "util/dto/CosmoFwd.h"

namespace cosmo::service::detail {

// Static utility for querying hardware and OS-level system information.
// Extracted from DeviceInfoServiceImpl to separate platform-specific
// I/O (reading /proc, /sys, calling bm_* APIs, pcap) from service logic.
class HardwareQueryUtil {
public:
    // ---- Device static info (read once at startup) ----

    // Read device SN and module type from /factory/OEMconfig.ini.
    // Retries up to 30 times with 1s sleep if SN is too short.
    static void ReadDeviceSnAndModel(std::string* device_sn, std::string* device_model);

    // Read hardware spec (eMMC CID) from sysfs.
    static std::string ReadHardwareSpec();

    // Execute `bm_version` and extract kernel version string.
    static std::string ReadKernelRevision();

    // ---- Hardware resource utilization (polled periodically) ----

    // Read /proc/stat and compute CPU usage ratio [0.0, 1.0].
    // Uses internal static state to compute delta between calls.
    static double QueryCpuUtilization();

    // Read /sys/class/thermal/thermal_zone0/temp.
    static float QueryCpuTemperature();

    // Query SOPHON BM1688 NPU via bm_dev_request + bm_get_stat.
    static cosmo::MsgGpuInfo QueryGpuUtilization();

    // Read /proc/meminfo for MemTotal and MemAvailable.
    static cosmo::MsgMemoryInfo QueryMemoryUtilization();

    // Query disk usage via statfs() on the application base directory.
    static cosmo::MsgDiskInfo QueryDiskUtilization();

    // Query available GPU/NPU heap memory in MB.
    static int64_t QueryAvailableGpuMemoryMB();

    // ---- Network interface queries ----

    // Get MAC address of the first network interface (via pcap + ioctl).
    static std::string QueryPrimaryMac();

    // Get IPv4 address of the first network interface (via pcap + ioctl).
    static std::string QueryPrimaryIPv4();

    // Get all network interfaces with their MAC addresses.
    static std::vector<std::pair<std::string, std::string>> QueryAllMacs();

private:
    // String parsing helpers (moved from HwInfoState).
    static bool AnalysisLine(const std::string& line, const std::string& key, std::string* value,
                             const std::string& equal_value = "=");
    static std::string KeepAlphaNum(const std::string& input);
    static std::string ExtractSpecContent(const std::string& input);
};

}  // namespace cosmo::service::detail
