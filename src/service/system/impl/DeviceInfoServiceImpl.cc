// DeviceInfoServiceImpl — Device Info Service Impl implementation.

#include "service/system/impl/DeviceInfoServiceImpl.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <thread>

#include "service/detail/ServiceRegistry.h"
#include "service/system/IAppInfoService.h"
#include "service/system/impl/HardwareQueryUtil.h"
#include "service/task/ITaskQuery.h"
#include "util/ErrorCode.h"
#include "util/FormatString.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/ScoreCalc.h"
#include "util/TimingConstants.h"
#include "util/Version.h"

namespace cosmo::service {

namespace fs = std::filesystem;

namespace {

    double ClampRatio(double value) {
        return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0;
    }

    struct CapacityUsage {
        int64_t total{0};
        int64_t available{0};
        int64_t used{0};
        int percent{0};
        bool valid{false};
    };

    CapacityUsage GetCapacityUsage(int64_t total, int64_t available) {
        CapacityUsage result;
        if (total <= 0 || available < 0) {
            return result;
        }
        result.total     = total;
        result.available = std::min(available, total);
        result.used      = total - result.available;
        result.percent   = static_cast<int>(
            std::lround(static_cast<double>(result.used) * 100.0 / static_cast<double>(result.total)));
        result.percent = std::clamp(result.percent, 0, 100);
        result.valid   = true;
        return result;
    }

    std::string FormatBinaryBytes(int64_t bytes) {
        constexpr double kMebibyte = 1024.0 * 1024.0;
        constexpr double kGibibyte = 1024.0 * kMebibyte;
        if (bytes >= static_cast<int64_t>(kGibibyte))
            return COSMO_FORMAT("{:.2f} GiB", static_cast<double>(bytes) / kGibibyte);
        return COSMO_FORMAT("{:.2f} MiB", static_cast<double>(bytes) / kMebibyte);
    }

    std::string FormatBinaryMebibytes(int64_t mebibytes) {
        if (mebibytes >= 1024)
            return COSMO_FORMAT("{:.2f} GiB", static_cast<double>(mebibytes) / 1024.0);
        return COSMO_FORMAT("{:.2f} MiB", static_cast<double>(mebibytes));
    }

}  // namespace

// Hardware info state (migrated from HwInfo singleton).
// Reads device static info once at construction via HardwareQueryUtil.
struct DeviceInfoServiceImpl::HwInfoState {
    std::string device_sn_;
    std::string device_model_{"CWAI-AIBOX"};
    std::string hw_revision_;
    std::string hw_spec_;

    HwInfoState() {
        detail::HardwareQueryUtil::ReadDeviceSnAndModel(&device_sn_, &device_model_);
        hw_spec_     = detail::HardwareQueryUtil::ReadHardwareSpec();
        hw_revision_ = detail::HardwareQueryUtil::ReadKernelRevision();
    }
};

// Hardware resource utilization state (migrated from HwResUtilization singleton).
// Owns a monitor thread that periodically polls hardware metrics via HardwareQueryUtil.
struct DeviceInfoServiceImpl::HwResState {
    std::atomic_bool thr_flag_{true};
    std::shared_mutex mtx_;
    double cpu_usage_{0.0};
    cosmo::MsgGpuInfo gpu_info_;
    cosmo::MsgMemoryInfo mem_info_;
#ifdef COSMO_NN_USE_AXERA_BACKEND
    detail::AxMemoryUsage ax_memory_;
#endif
    cosmo::MsgDiskInfo disk_info_;
    cosmo::MsgNetInfo net_info_;
    uint32_t gpu_device_cnt_{1};
    std::thread monitor_thread_;

    HwResState() {
        monitor_thread_ = std::thread([this]() { Run(); });
    }

    ~HwResState() {
        thr_flag_.store(false);
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
    }

    void Run() {
        int index = 0;
        while (thr_flag_.load()) {
            if (index == 0) {
                auto cpu  = detail::HardwareQueryUtil::QueryCpuUtilization();
                auto disk = detail::HardwareQueryUtil::QueryDiskUtilization();
                auto mem  = detail::HardwareQueryUtil::QueryMemoryUtilization();
                auto temp = detail::HardwareQueryUtil::QueryCpuTemperature();
                auto gpu  = detail::HardwareQueryUtil::QueryGpuUtilization();
#ifdef COSMO_NN_USE_AXERA_BACKEND
                auto ax_mem = detail::HardwareQueryUtil::QueryAxMemoryUsage(mem.memtotal);
#endif
                {
                    std::lock_guard<std::shared_mutex> lock(mtx_);
                    cpu_usage_ = cpu;
                    mem_info_  = mem;
                    disk_info_ = disk;
                    gpu_info_  = gpu;
#ifdef COSMO_NN_USE_AXERA_BACKEND
                    ax_memory_ = ax_mem;
#endif
                }
                LOG_INFO(
                    "Device resource - CPU:{:.2f}%, Temp:{:.2f}C Avail/Total Mem:{}/{} MB, Disk:{}/{} GB",
                    cpu * 100, temp, mem.memavailable / (1024 * 1024), mem.memtotal / (1024 * 1024),
                    disk.diskavailable / (1024 * 1024 * 1024), disk.disktotal / (1024 * 1024 * 1024));
            }

            index += 1;
            std::this_thread::sleep_for(timing::kHalfSecondInterval);
            if (index >= 10) {
                index = 0;
            }
        }
    }

    // Cached getters
    double GetCpuUtilization() {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        return cpu_usage_;
    }
    cosmo::MsgGpuInfo GetGpuUtilization() {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        return gpu_info_;
    }
    cosmo::MsgMemoryInfo GetMemoryUtilization() {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        return mem_info_;
    }
#ifdef COSMO_NN_USE_AXERA_BACKEND
    std::pair<cosmo::MsgMemoryInfo, detail::AxMemoryUsage> GetAxMemorySnapshot() {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        return {mem_info_, ax_memory_};
    }
#endif
    cosmo::MsgDiskInfo GetDiskUtilization() {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        return disk_info_;
    }
    cosmo::MsgNetInfo GetNetUtilization() {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        return net_info_;
    }
    size_t GetGpuNum() {
        return gpu_device_cnt_;
    }

    std::string GetDevId() {
        std::string mac = detail::HardwareQueryUtil::QueryPrimaryMac();
        if ("-1" == mac) {
            LOG_ERRO("{}", "Fail To get MAC!");
            return "Get DevId Failed";
        }
        return "CWAI_Analyzer_" + mac;
    }
};

DeviceInfoServiceImpl::DeviceInfoServiceImpl()
    : hw_info_state_(std::make_unique<HwInfoState>()), hw_res_state_(std::make_unique<HwResState>()) {}

DeviceInfoServiceImpl::~DeviceInfoServiceImpl() {
    hw_res_state_.reset();
}

DeviceBasicInfo DeviceInfoServiceImpl::GetDeviceInfo() {
    DeviceBasicInfo info;
    info.devModel        = hw_info_state_->device_model_;
    info.devVersion      = hw_info_state_->hw_revision_;
    info.softwareVersion = cosmo::util::GetAbbrVersion();
    info.devSn           = hw_info_state_->device_sn_;
    info.appRuntime = service::ServiceRegistry::Instance().Get<service::IAppInfoService>().GetAppRuntime();
    return info;
}

std::string DeviceInfoServiceImpl::GetDevSn() {
    return hw_info_state_->device_sn_;
}
std::string DeviceInfoServiceImpl::GetDevModel() {
    return hw_info_state_->device_model_;
}
std::string DeviceInfoServiceImpl::GetDevVersion() {
    return hw_info_state_->hw_revision_;
}
std::string DeviceInfoServiceImpl::GetDevSpec() {
    return hw_info_state_->hw_spec_;
}

std::vector<HwResourceItem> DeviceInfoServiceImpl::GetHardwareResource(double& custom_score) {
    std::vector<HwResourceItem> items;

    // CPU
    const auto raw_cpu_utl = hw_res_state_->GetCpuUtilization();
    const auto cpu_utl     = ClampRatio(raw_cpu_utl);
    items.push_back({"cpuUtilization", "CPU使用率", static_cast<int>(std::lround(cpu_utl * 100)),
                     COSMO_FORMAT("{:.0f}%", cpu_utl * 100), COSMO_FORMAT("{:.0f}%", (1 - cpu_utl) * 100),
                     std::isfinite(raw_cpu_utl) && raw_cpu_utl >= 0.0 ? 1 : 0, ""});

    // Memory
#ifdef COSMO_NN_USE_AXERA_BACKEND
    const auto [mem_utl, ax_mem] = hw_res_state_->GetAxMemorySnapshot();
#else
    const auto mem_utl = hw_res_state_->GetMemoryUtilization();
#endif
    const auto mem_usage = GetCapacityUsage(mem_utl.memtotal, mem_utl.memavailable);
    items.push_back({"generalMemoryUtilization", "系统内存使用率", mem_usage.percent,
                     FormatBinaryBytes(mem_usage.used), FormatBinaryBytes(mem_usage.available),
                     mem_usage.valid ? 1 : 0, "system",
                     mem_usage.valid ? FormatBinaryBytes(mem_usage.total) : ""});
#ifdef COSMO_NN_USE_AXERA_BACKEND
    if (ax_mem.linuxAllocatedBytes > 0)
        items.back().allocatedSize = FormatBinaryBytes(ax_mem.linuxAllocatedBytes);

    const auto cmm_usage = GetCapacityUsage(ax_mem.cmmTotalBytes, ax_mem.cmmAvailableBytes);
    items.push_back({"axCmmMemoryUtilization", "AX CMM内存使用率", cmm_usage.percent,
                     cmm_usage.valid ? FormatBinaryBytes(cmm_usage.used) : "--",
                     cmm_usage.valid ? FormatBinaryBytes(cmm_usage.available) : "--", cmm_usage.valid ? 1 : 0,
                     "ax-cmm", cmm_usage.valid ? FormatBinaryBytes(cmm_usage.total) : ""});
    const bool total_available = ax_mem.bootAllocatedBytes > 0;
    items.push_back({"memoryAllocationTotal", "启动内存分配总量", 0, "--", "--", total_available ? 1 : 0,
                     "boot-allocation", total_available ? FormatBinaryBytes(ax_mem.bootAllocatedBytes) : ""});
#endif

    // GPU/NPU
    auto gpu_utl             = hw_res_state_->GetGpuUtilization();
    const auto raw_gpu_usage = gpu_utl.gpuusage;
    const auto gpu_usage     = ClampRatio(raw_gpu_usage);
    gpu_utl.gpuusage         = gpu_usage;
    if (gpu_utl.gpuusageAvailable) {
        items.push_back({"npuUtilization", "NPU负载", static_cast<int>(std::lround(gpu_usage * 100)),
                         COSMO_FORMAT("{:.0f}%", gpu_usage * 100),
                         COSMO_FORMAT("{:.0f}%", (1 - gpu_usage) * 100),
                         std::isfinite(raw_gpu_usage) && raw_gpu_usage >= 0.0 ? 1 : 0, ""});
    } else {
        items.push_back({"npuUtilization", "NPU负载", 0, "--", "--", 0, ""});
    }

    // GPU memory details
    auto add_gpu_mem_item = [&](const std::string& key, const std::string& name,
                                const std::string& memory_domain, const auto& dev) {
        const auto usage = GetCapacityUsage(dev.gpumemtotal, dev.gpumemavailable);
        items.push_back({key, name, usage.percent, FormatBinaryMebibytes(usage.used),
                         FormatBinaryMebibytes(usage.available), usage.valid ? 1 : 0, memory_domain});
    };

    // Rockchip RKNN devices use shared system memory: legacy gpumem fields describe the
    // same system DDR already represented above. Keep those fields for model
    // admission and wire compatibility, but never emit a second UI capacity.
    if (gpu_utl.memoryDomain != "shared-system") {
        if (2 == gpu_utl.gpudevusage.size()) {
            add_gpu_mem_item("modelMemoryUtilization", "模型内存使用率", "accelerator-heap-0",
                             gpu_utl.gpudevusage[0]);
            add_gpu_mem_item("pictureMemoryUtilization", "图片内存使用率", "accelerator-heap-1",
                             gpu_utl.gpudevusage[1]);
        } else if (3 == gpu_utl.gpudevusage.size()) {
            add_gpu_mem_item("modelMemoryUtilization", "heap 0 内存使用率", "accelerator-heap-0",
                             gpu_utl.gpudevusage[0]);
            add_gpu_mem_item("pictureMemoryUtilization", "heap 1 内存使用率", "accelerator-heap-1",
                             gpu_utl.gpudevusage[1]);
            add_gpu_mem_item("TPPMemoryUtilization", "heap 2 内存使用率", "accelerator-heap-2",
                             gpu_utl.gpudevusage[2]);
        } else {
            const auto gpu_mem_usage = GetCapacityUsage(gpu_utl.gpumemtotal, gpu_utl.gpumemavailable);
            items.push_back({"specialMemoryUtilization", "芯片内存使用率", gpu_mem_usage.percent,
                             FormatBinaryMebibytes(gpu_mem_usage.used),
                             FormatBinaryMebibytes(gpu_mem_usage.available), gpu_mem_usage.valid ? 1 : 0,
                             "accelerator"});
        }
    }

    // Disk
    const auto disk_utl   = hw_res_state_->GetDiskUtilization();
    const auto disk_usage = GetCapacityUsage(disk_utl.disktotal, disk_utl.diskavailable);
    items.push_back({"eMMCUtilization", "eMMC使用率", disk_usage.percent, FormatBinaryBytes(disk_usage.used),
                     FormatBinaryBytes(disk_usage.available), disk_usage.valid ? 1 : 0, ""});

    // Packet stats
    size_t packet_total = 0, packet_proc = 0, packet_discard = 0, continues_discard_sec = 0;
    ServiceRegistry::Instance().Get<ITaskQuery>().PacketStatus(packet_total, packet_proc, packet_discard,
                                                               continues_discard_sec);
    packet_discard      = std::min(packet_discard, packet_total);
    double used_percent = 0.0;
    if (packet_total > 0) {
        used_percent = static_cast<double>(packet_discard) / packet_total;
    }
    used_percent = ClampRatio(used_percent);
    items.push_back({"packetDiscardUtilization", "丢包率", static_cast<int>(std::lround(used_percent * 100)),
                     COSMO_FORMAT("{}个", packet_discard),
                     COSMO_FORMAT("{}个", packet_total - packet_discard), 1, ""});
    LOG_DEBUG("continuesDiscardSec:{} packetDiscard:{}", continues_discard_sec, packet_discard);

    std::vector<cosmo::GpuMemSnapshot> devs;
    devs.reserve(gpu_utl.gpudevusage.size());
    std::transform(gpu_utl.gpudevusage.begin(), gpu_utl.gpudevusage.end(), std::back_inserter(devs),
                   [](const auto& dev) -> cosmo::GpuMemSnapshot {
                       const auto usage = GetCapacityUsage(dev.gpumemtotal, dev.gpumemavailable);
                       return {usage.total, usage.available};
                   });
    const auto aggregate_gpu_memory = GetCapacityUsage(gpu_utl.gpumemtotal, gpu_utl.gpumemavailable);
    custom_score =
        cosmo::CalcCustomScore(gpu_usage, aggregate_gpu_memory.total, aggregate_gpu_memory.available, devs,
                               used_percent, continues_discard_sec);
    return items;
}

double DeviceInfoServiceImpl::GetCpuUtilization() {
    return hw_res_state_->GetCpuUtilization();
}
cosmo::MsgGpuInfo DeviceInfoServiceImpl::GetGpuUtilization() {
    return hw_res_state_->GetGpuUtilization();
}
cosmo::MsgMemoryInfo DeviceInfoServiceImpl::GetMemoryUtilization() {
    return hw_res_state_->GetMemoryUtilization();
}
cosmo::MsgDiskInfo DeviceInfoServiceImpl::GetDiskUtilization() {
    return hw_res_state_->GetDiskUtilization();
}
cosmo::MsgNetInfo DeviceInfoServiceImpl::GetNetUtilization() {
    return hw_res_state_->GetNetUtilization();
}
std::vector<std::pair<std::string, std::string>> DeviceInfoServiceImpl::GetMacs() {
    return detail::HardwareQueryUtil::QueryAllMacs();
}
std::string DeviceInfoServiceImpl::GetDevId() {
    return hw_res_state_->GetDevId();
}
std::string DeviceInfoServiceImpl::GetIPV4() {
    return detail::HardwareQueryUtil::QueryPrimaryIPv4();
}
int64_t DeviceInfoServiceImpl::GetAvailableGpuMemoryMB() {
    return detail::HardwareQueryUtil::QueryAvailableGpuMemoryMB();
}
size_t DeviceInfoServiceImpl::GetGpuNum() {
    return hw_res_state_->GetGpuNum();
}

}  // namespace cosmo::service
