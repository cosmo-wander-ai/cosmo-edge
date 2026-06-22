// DeviceInfoServiceImpl — Device Info Service Impl implementation.

#include "service/system/impl/DeviceInfoServiceImpl.h"

#include <atomic>
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
    cosmo::MsgDiskInfo disk_info_;
    cosmo::MsgNetInfo net_info_;
    uint32_t gpu_device_cnt_{1};
    std::string dev_id_string_;
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
                {
                    std::lock_guard<std::shared_mutex> lock(mtx_);
                    cpu_usage_ = cpu;
                    mem_info_  = mem;
                    disk_info_ = disk;
                    gpu_info_  = gpu;
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
        dev_id_string_ = "CWAI_Analyzer_" + mac;
        return dev_id_string_;
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
    auto cpu_utl = hw_res_state_->GetCpuUtilization();
    items.push_back({"cpuUtilization", "CPU使用率", static_cast<int>(cpu_utl * 100 + 0.5),
                     COSMO_FORMAT("{:.0f}%", cpu_utl * 100), COSMO_FORMAT("{:.0f}%", (1 - cpu_utl) * 100),
                     1});

    // Memory
    auto mem_utl = hw_res_state_->GetMemoryUtilization();
    int mem_percent =
        static_cast<int>((mem_utl.memtotal - mem_utl.memavailable) * 100.0 / mem_utl.memtotal + 0.5);
    items.push_back({"generalMemoryUtilization", "业务内存使用率", mem_percent,
                     COSMO_FORMAT("{:.2f} MB",
                                  static_cast<double>(mem_utl.memtotal - mem_utl.memavailable) / 1024 / 1024),
                     COSMO_FORMAT("{:.2f} MB", static_cast<double>(mem_utl.memavailable) / 1024 / 1024), 1});

    // GPU/NPU
    auto gpu_utl = hw_res_state_->GetGpuUtilization();
    items.push_back({"npuUtilization", "NPU使用率", static_cast<int>(gpu_utl.gpuusage * 100 + 0.5),
                     COSMO_FORMAT("{:.0f}%", gpu_utl.gpuusage * 100),
                     COSMO_FORMAT("{:.0f}%", (1 - gpu_utl.gpuusage) * 100), 1});

    // GPU memory details
    auto add_gpu_mem_item = [&](const std::string& key, const std::string& name, const auto& dev) {
        int percent =
            static_cast<int>((dev.gpumemtotal - dev.gpumemavailable) * 100.0 / dev.gpumemtotal + 0.5);
        items.push_back(
            {key, name, percent,
             COSMO_FORMAT("{:.2f} GB", static_cast<double>(dev.gpumemtotal - dev.gpumemavailable) / 1024),
             COSMO_FORMAT("{:.2f} GB", static_cast<double>(dev.gpumemavailable) / 1024), 1});
    };

    if (2 == gpu_utl.gpudevusage.size()) {
        add_gpu_mem_item("modelMemoryUtilization", "模型内存使用率", gpu_utl.gpudevusage[0]);
        add_gpu_mem_item("pictureMemoryUtilization", "图片内存使用率", gpu_utl.gpudevusage[1]);
    } else if (3 == gpu_utl.gpudevusage.size()) {
        add_gpu_mem_item("modelMemoryUtilization", "heap 0 内存使用率", gpu_utl.gpudevusage[0]);
        add_gpu_mem_item("pictureMemoryUtilization", "heap 1 内存使用率", gpu_utl.gpudevusage[1]);
        add_gpu_mem_item("TPPMemoryUtilization", "heap 2 内存使用率", gpu_utl.gpudevusage[2]);
    } else {
        int gpu_mem_percent = static_cast<int>(
            (gpu_utl.gpumemtotal - gpu_utl.gpumemavailable) * 100.0 / gpu_utl.gpumemtotal + 0.5);
        items.push_back(
            {"specialMemoryUtilization", "芯片内存使用率", gpu_mem_percent,
             COSMO_FORMAT("{:.2f} GB",
                          static_cast<double>(gpu_utl.gpumemtotal - gpu_utl.gpumemavailable) / 1024),
             COSMO_FORMAT("{:.2f} GB", static_cast<double>(gpu_utl.gpumemavailable) / 1024), 1});
    }

    // Disk
    auto disk_utl = hw_res_state_->GetDiskUtilization();
    int disk_percent =
        static_cast<int>((disk_utl.disktotal - disk_utl.diskavailable) * 100.0 / disk_utl.disktotal + 0.5);
    items.push_back(
        {"eMMCUtilization", "eMMC使用率", disk_percent,
         COSMO_FORMAT("{:.2f} GB",
                      static_cast<double>(disk_utl.disktotal - disk_utl.diskavailable) / 1024 / 1024 / 1024),
         COSMO_FORMAT("{:.2f} GB", static_cast<double>(disk_utl.diskavailable) / 1024 / 1024 / 1024), 1});

    // Packet stats
    size_t packet_total = 0, packet_proc = 0, packet_discard = 0, continues_discard_sec = 0;
    ServiceRegistry::Instance().Get<ITaskQuery>().PacketStatus(packet_total, packet_proc, packet_discard,
                                                               continues_discard_sec);
    double used_percent = 0.0;
    if (packet_total > 0) {
        used_percent = static_cast<double>(packet_discard) / packet_total;
    }
    items.push_back({"packetDiscardUtilization", "丢包率", static_cast<int>(used_percent * 100),
                     COSMO_FORMAT("{}个", packet_discard),
                     COSMO_FORMAT("{}个", packet_total - packet_discard), 1});
    LOG_INFO("continuesDiscardSec:{} packetDiscard:{}", continues_discard_sec, packet_discard);

    std::vector<cosmo::GpuMemSnapshot> devs;
    devs.reserve(gpu_utl.gpudevusage.size());
    std::transform(gpu_utl.gpudevusage.begin(), gpu_utl.gpudevusage.end(), std::back_inserter(devs),
                   [](const auto& d) -> cosmo::GpuMemSnapshot {
                       return {d.gpumemtotal, d.gpumemavailable};
                   });
    custom_score = cosmo::CalcCustomScore(gpu_utl.gpuusage, gpu_utl.gpumemtotal, gpu_utl.gpumemavailable,
                                          devs, used_percent, continues_discard_sec);
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
