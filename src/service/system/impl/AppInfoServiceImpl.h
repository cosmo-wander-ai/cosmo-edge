// AppInfoService implementation — owns app-info state previously held by UserDataUtil.
// System overviews read utilization through IHardwareQuery.
#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>

#include "service/system/IAppInfoService.h"

namespace cosmo::service {

class AppInfoServiceImpl : public IAppInfoService {
public:
    AppInfoServiceImpl();

    // ── Init-time setters (NOT on interface, called from app_init before DI is ready) ──
    void SetDevId(std::string devId);
    void SetEngineType(std::string engineType);

    // ── UserDataUtil ──
    bool GetHaveManager() override;
    std::string GetEngineType() override;
    std::string DevId() override;
    int64_t GetAppRuntime() override;
    int GetPicTaskGroupCount() override;
    std::string UserDataPath() override;
    std::string GetTaskOverviewDataPath() override;
    void SetOverviewStructureRecord(bool value) override;
    bool GetOverviewStructureRecord() override;
    void SetOverviewStructureFile(bool value) override;
    bool GetOverviewStructureFile() override;
    bool GetModelDebug() override;
    size_t GetNumber() override;
    std::string LogPath() override;
    std::string LogWebPath() override;

    // ── ModelPathUtil ──
    void SetModelPath(const std::string& algCode, const std::string& modelPath) override;

    // ── MemoryPoolMng ──
    std::string OutputMallocBuf() override;
    std::vector<PoolStatusDto> GetMemoryPoolStatus() override;

    // ── System Overviews and Logging proxies ──
    cosmo::MsgQueryLogsSend GetPagedLogs(const cosmo::MsgQueryLogsRecv& data,
                                         std::error_condition& errc) override;
    cosmo::MsgInfoSend GetSystemOverviewInfo(const cosmo::MsgInfoRecv& data,
                                             std::error_condition& errc) override;

private:
    mutable std::mutex mtx_;
    std::string engine_type_;
    std::string dev_id_;
    std::chrono::steady_clock::time_point start_time_;
    std::atomic<bool> overview_structure_record_{false};
    std::atomic<bool> overview_structure_file_{false};
    std::atomic<size_t> number_{0};
};

}  // namespace cosmo::service
