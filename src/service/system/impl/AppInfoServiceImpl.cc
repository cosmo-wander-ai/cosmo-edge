// AppInfoServiceImpl — AppInfoService implementation — owns app-info state previously held by UserDa...

#include "service/system/impl/AppInfoServiceImpl.h"

#include <cctype>
#include <filesystem>
#include <limits>
#include <optional>

#include "mem/MemoryPoolMng.h"
#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelPathMapping.h"
#include "service/system/IDeviceInfoService.h"
#include "service/task/ITaskQuery.h"
#include "util/DateTimeFormat.h"
#include "util/Exception.h"
#include "util/FormatString.h"
#include "util/PathUtil.h"
#include "util/ScoreCalc.h"

namespace cosmo::service {

namespace {

    constexpr size_t kLogTimestampLength = 15;
    constexpr int kMaxLogPageSize        = 1000;

    std::optional<std::string> ExtractLogTimestamp(const std::string& filename) {
        if (filename.size() < kLogTimestampLength) {
            return std::nullopt;
        }

        for (size_t offset = 0; offset <= filename.size() - kLogTimestampLength; ++offset) {
            const auto candidate = std::string_view(filename).substr(offset, kLogTimestampLength);
            if (candidate[8] != '-') {
                continue;
            }

            bool digits_only = true;
            for (size_t index = 0; index < candidate.size(); ++index) {
                if (index != 8 && !std::isdigit(static_cast<unsigned char>(candidate[index]))) {
                    digits_only = false;
                    break;
                }
            }
            if (!digits_only) {
                continue;
            }

            try {
                const std::string date = std::string(candidate.substr(0, 4)) + "-" +
                                         std::string(candidate.substr(4, 2)) + "-" +
                                         std::string(candidate.substr(6, 2));
                const std::string time = std::string(candidate.substr(9, 2)) + ":" +
                                         std::string(candidate.substr(11, 2)) + ":" +
                                         std::string(candidate.substr(13, 2));
                (void)cosmo::util::YMDDate(date);
                (void)cosmo::util::HMSTime(time);
                return std::string(candidate);
            } catch (const cosmo::util::Exception&) {
                continue;
            }
        }
        return std::nullopt;
    }

    bool CompareLogNamesDescending(const std::string& lhs, const std::string& rhs) {
        const auto lhs_timestamp = ExtractLogTimestamp(lhs);
        const auto rhs_timestamp = ExtractLogTimestamp(rhs);
        if (lhs_timestamp.has_value() != rhs_timestamp.has_value()) {
            return lhs_timestamp.has_value();
        }
        if (lhs_timestamp != rhs_timestamp) {
            return lhs_timestamp > rhs_timestamp;
        }
        return lhs > rhs;
    }

}  // namespace

// ── Constructor ──

AppInfoServiceImpl::AppInfoServiceImpl() : start_time_(std::chrono::steady_clock::now()) {}

void AppInfoServiceImpl::SetDevId(std::string dev_id) {
    std::lock_guard<std::mutex> lck(mtx_);
    dev_id_ = std::move(dev_id);
}

void AppInfoServiceImpl::SetEngineType(std::string engine_type) {
    std::lock_guard<std::mutex> lck(mtx_);
    engine_type_ = std::move(engine_type);
}

// ── UserDataUtil ──

bool AppInfoServiceImpl::GetHaveManager() {
    return have_manager_;
}

std::string AppInfoServiceImpl::GetEngineType() {
    std::lock_guard<std::mutex> lck(mtx_);
    return engine_type_;
}

std::string AppInfoServiceImpl::DevId() {
    std::lock_guard<std::mutex> lck(mtx_);
    return dev_id_;
}

int64_t AppInfoServiceImpl::GetAppRuntime() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count() / 1000;
}

int AppInfoServiceImpl::GetPicTaskGroupCount() {
    return pic_task_group_count_;
}

std::string AppInfoServiceImpl::UserDataPath() {
    return cosmo::path::GetBaseDir() + "/cwai";
}

std::string AppInfoServiceImpl::GetTaskOverviewDataPath() {
    std::string path = cosmo::path::GetBaseDir() + "/cwai/overview/";
    std::filesystem::create_directories(path);
    return path;
}

void AppInfoServiceImpl::SetOverviewStructureRecord(bool value) {
    overview_structure_record_ = value;
}

bool AppInfoServiceImpl::GetOverviewStructureRecord() {
    return overview_structure_record_;
}

void AppInfoServiceImpl::SetOverviewStructureFile(bool value) {
    overview_structure_file_ = value;
}

bool AppInfoServiceImpl::GetOverviewStructureFile() {
    return overview_structure_file_;
}

bool AppInfoServiceImpl::GetModelDebug() {
    return model_debug_;
}

size_t AppInfoServiceImpl::GetNumber() {
    return number_.fetch_add(1, std::memory_order_relaxed);
}

std::string AppInfoServiceImpl::LogPath() {
    // Consistent with LogInit in application.cc: GetLogPath() + "/logs/"
    return cosmo::path::GetLogPath() + "/logs/";
}

std::string AppInfoServiceImpl::LogWebPath() {
    return "/logs/";
}

// ── HwResUtilization ──

double AppInfoServiceImpl::GetCpuUtilization() {
    return ServiceRegistry::Instance().Get<IDeviceInfoService>().GetCpuUtilization();
}

cosmo::MsgGpuInfo AppInfoServiceImpl::GetGpuUtilization() {
    return ServiceRegistry::Instance().Get<IDeviceInfoService>().GetGpuUtilization();
}

cosmo::MsgMemoryInfo AppInfoServiceImpl::GetMemoryUtilization() {
    return ServiceRegistry::Instance().Get<IDeviceInfoService>().GetMemoryUtilization();
}

cosmo::MsgDiskInfo AppInfoServiceImpl::GetDiskUtilization() {
    return ServiceRegistry::Instance().Get<IDeviceInfoService>().GetDiskUtilization();
}

cosmo::MsgNetInfo AppInfoServiceImpl::GetNetUtilization() {
    return ServiceRegistry::Instance().Get<IDeviceInfoService>().GetNetUtilization();
}

int64_t AppInfoServiceImpl::GetAvailableGpuMemoryMB() {
    return ServiceRegistry::Instance().Get<IDeviceInfoService>().GetAvailableGpuMemoryMB();
}

size_t AppInfoServiceImpl::GetGpuNum() {
    return ServiceRegistry::Instance().Get<IDeviceInfoService>().GetGpuNum();
}

// ── ModelPathUtil ──

void AppInfoServiceImpl::SetModelPath(const std::string& algCode, const std::string& modelPath) {
    ServiceRegistry::Instance().Get<IModelPathMapping>().SetModelPathMapping(algCode, modelPath);
}

// ── MemoryPoolMng ──

std::string AppInfoServiceImpl::OutputMallocBuf() {
    return cosmo::mem::GetMemoryPool().OutputMallocBuf();
}

std::vector<PoolStatusDto> AppInfoServiceImpl::GetMemoryPoolStatus() {
    auto raw = cosmo::mem::GetMemoryPool().Status();
    std::vector<PoolStatusDto> result;
    result.reserve(raw.size());
    for (auto& s : raw) {
        PoolStatusDto dto;
        dto.pool_size = s.pool_size;
        dto.idle_cnt  = s.idle_cnt;
        dto.used_cnt  = s.used_cnt;
        for (auto& b : s.used_nodes_status) {
            dto.used_nodes_status.push_back({b.thread_id, b.duration, b.malloc_timepoint});
        }
        result.push_back(std::move(dto));
    }
    return result;
}

// ── System Overviews and Logging proxies ──

cosmo::MsgQueryLogsSend AppInfoServiceImpl::GetPagedLogs(const cosmo::MsgQueryLogsRecv& data,
                                                         std::error_condition& errc) {
    cosmo::MsgQueryLogsSend retData{};
    if (data.pageNum < 1 || data.pageSize < 1 || data.pageSize > kMaxLogPageSize) {
        errc = cosmo::util::ErrorEnum::ParameterException;
        return retData;
    }

    std::vector<std::string> files;
    std::error_code filesystem_error;
    std::filesystem::directory_iterator entry(LogPath(), filesystem_error);
    const std::filesystem::directory_iterator end;
    while (!filesystem_error && entry != end) {
        std::error_code status_error;
        const auto status = entry->symlink_status(status_error);
        if (!status_error && status.type() == std::filesystem::file_type::regular) {
            files.push_back(entry->path().filename().string());
        }
        entry.increment(filesystem_error);
    }
    if (filesystem_error) {
        LOG_WARN("Failed to enumerate log directory {}: {}", LogPath(), filesystem_error.message());
    }
    LOG_INFO("Get File From :{} File Size:{}", LogPath(), files.size());
    std::sort(files.begin(), files.end(), CompareLogNamesDescending);

    retData.resData.totalCount = files.size() > static_cast<size_t>(std::numeric_limits<int>::max())
                                     ? std::numeric_limits<int>::max()
                                     : static_cast<int>(files.size());
    retData.resData.path       = LogWebPath();
    const auto page_index      = static_cast<size_t>(data.pageNum - 1);
    const auto page_size       = static_cast<size_t>(data.pageSize);
    if (page_index > std::numeric_limits<size_t>::max() / page_size) {
        return retData;
    }
    const size_t index = page_index * page_size;
    if (index >= files.size()) {
        return retData;
    }
    if (page_size > std::numeric_limits<size_t>::max() - index) {
        return retData;
    }
    const size_t index_max = std::min(files.size(), index + page_size);
    retData.resData.logs.insert(retData.resData.logs.end(), files.begin() + index, files.begin() + index_max);
    LOG_INFO("index:{} index_max:{} logs.size:{}", index, index_max, retData.resData.logs.size());

    return retData;
}

cosmo::MsgInfoSend AppInfoServiceImpl::GetSystemOverviewInfo(const cosmo::MsgInfoRecv& data,
                                                             std::error_condition& errc) {
    cosmo::MsgInfoSend retData{};

    auto dev_id = DevId();
    LOG_INFO("Info: Param:{} Local:{}", data.devId, dev_id);
    if (dev_id != data.devId) {
        errc = cosmo::util::ErrorEnum::InvalidParam;
        return retData;
    }

    retData.devId           = dev_id;
    retData.runtimeDuration = GetAppRuntime();

    retData.cpuUsage     = static_cast<float>(GetCpuUtilization());
    auto gpu_info        = GetGpuUtilization();
    auto mem_info        = GetMemoryUtilization();
    auto disk_info       = GetDiskUtilization();
    auto net_info        = GetNetUtilization();
    retData.memTotal     = mem_info.memtotal;
    retData.memAvailable = mem_info.memavailable;

    retData.gpuUsage        = static_cast<float>(gpu_info.gpuusage);
    retData.gpuMemTotal     = gpu_info.gpumemtotal;
    retData.gpuMemAvailable = gpu_info.gpumemavailable;
    retData.gpuCapacity     = gpu_info.gpuCapacity;
    if (2 == gpu_info.gpudevusage.size()) {
        cosmo::MsgMemInfo memInfoMode;
        memInfoMode.name             = "模型内存";
        memInfoMode.memTotal         = gpu_info.gpudevusage[0].gpumemtotal;
        memInfoMode.memAvailable     = gpu_info.gpudevusage[0].gpumemavailable;
        retData.gpuModelMemTotal     = memInfoMode.memTotal;
        retData.gpuModelMemAvailable = memInfoMode.memAvailable;
        retData.gpuMemDetails.push_back(memInfoMode);

        cosmo::MsgMemInfo memInfoPic;
        memInfoPic.name            = "图片内存";
        memInfoPic.memTotal        = gpu_info.gpudevusage[1].gpumemtotal;
        memInfoPic.memAvailable    = gpu_info.gpudevusage[1].gpumemavailable;
        retData.gpuPicMemTotal     = memInfoPic.memTotal;
        retData.gpuPicMemAvailable = memInfoPic.memAvailable;
        retData.gpuMemDetails.push_back(memInfoPic);
    }

    retData.diskTotal     = disk_info.disktotal;
    retData.diskAvailable = disk_info.diskavailable;

    retData.networkUpperrate    = net_info.networkupperrate;
    retData.networkDownwardrate = net_info.networkdownwardrate;

    std::vector<cosmo::AlgActionDataQueueStatusDto> que_statuss;
    ServiceRegistry::Instance().Get<ITaskQuery>().QueueStatusDto(que_statuss, 30);
    std::map<std::string, cosmo::AlgActionNodeDurationInfo> durationInfos;
    size_t discard_max_sec = 0;
    for (auto& queEl : que_statuss) {
        retData.insertCount += queEl.queueStatus.insertCount;
        retData.processCount += queEl.queueStatus.processCount;
        retData.discardCount += queEl.queueStatus.discardCount;
        retData.insertCountPeriod += queEl.queueStatus.insertCountPeriod;
        retData.processCountPeriod += queEl.queueStatus.processCountPeriod;
        retData.discardCountPeriod += queEl.queueStatus.discardCountPeriod;
        if (queEl.queueStatus.continuousDiscardCountPeriod > discard_max_sec) {
            discard_max_sec = queEl.queueStatus.continuousDiscardCountPeriod;
        }

        for (const auto& durationInfo : queEl.durationInfos) {
            auto& idData = durationInfos[durationInfo.name];
            idData.durationUs += (durationInfo.duration_ns / 1000);
            idData.durationCount += durationInfo.count;
            int64_t value = durationInfo.duration_max_ns / 1000;
            if (value > idData.durationMaxUs) {
                idData.durationMaxUs = value;
            }
            value = durationInfo.cost_max_ns / 1000;
            if (value > idData.costMaxUs) {
                idData.costMaxUs = value;
            }
            value = durationInfo.duration_min_ns / 1000;
            if (((idData.durationMinUs != 0) && (value < idData.durationMinUs)) ||
                (idData.durationMinUs == 0)) {
                idData.durationMinUs = value;
            }
            value = durationInfo.cost_min_ns / 1000;
            if (((idData.costMinUs != 0) && (value < idData.costMinUs)) || (idData.costMinUs == 0)) {
                idData.costMinUs = value;
            }
        }
    }

    double discard_packet_ratio = 0.0;
    if (retData.insertCountPeriod > 0) {
        discard_packet_ratio = static_cast<double>(retData.discardCountPeriod) / retData.insertCountPeriod;
        retData.packetDiscardRate = COSMO_FORMAT("{:.4f}", discard_packet_ratio);
        LOG_INFO(" discard_packet_ratio:{}  discardCountPeriod:{} insertCountPeriod:{}", discard_packet_ratio,
                 retData.discardCountPeriod, retData.insertCountPeriod);
    }

    std::vector<cosmo::GpuMemSnapshot> devs;
    devs.reserve(gpu_info.gpudevusage.size());
    std::transform(gpu_info.gpudevusage.begin(), gpu_info.gpudevusage.end(), std::back_inserter(devs),
                   [](const auto& d) -> cosmo::GpuMemSnapshot { return {d.gpumemtotal, d.gpumemavailable}; });
    double custom_score =
        cosmo::CalcCustomScore(gpu_info.gpuusage, gpu_info.gpumemtotal, gpu_info.gpumemavailable, devs,
                               discard_packet_ratio, discard_max_sec);
    retData.customScore = COSMO_FORMAT("{:.4f}", custom_score);

    for (auto it = durationInfos.begin(); it != durationInfos.end(); ++it) {
        cosmo::AlgActionNodeDurationInfo info;
        info.name          = it->first;
        info.durationUs    = it->second.durationUs;
        info.durationCount = it->second.durationCount;
        info.durationMaxUs = it->second.durationMaxUs;
        info.durationMinUs = it->second.durationMinUs;
        info.costMaxUs     = it->second.costMaxUs;
        info.costMinUs     = it->second.costMinUs;
        if (info.durationCount > 0)
            info.durationAvgUs = it->second.durationUs / it->second.durationCount;
        retData.nodeDurationInfos.push_back(info);
    }

    return retData;
}

}  // namespace cosmo::service
