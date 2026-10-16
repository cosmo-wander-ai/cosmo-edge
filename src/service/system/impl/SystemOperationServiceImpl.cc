// SystemOperationServiceImpl — System Operation Service Impl implementation.

#include "service/system/impl/SystemOperationServiceImpl.h"

#include <chrono>
#include <filesystem>
#include <mutex>

#include "service/detail/ServiceRegistry.h"
#include "service/system/impl/LogArchive.h"
#include "service/system/impl/PacketUpgrade.h"
#include "service/system/impl/UpgradeStorage.h"
#include "util/ErrorCode.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/ThreadRegistry.h"
#include "util/TimeUtil.h"
#include "util/UuidUtil.h"

namespace cosmo::service {

namespace fs = std::filesystem;

void SystemOperationServiceImpl::RebootDevice(const std::string& reason) {
    reboot_mgr_.Reboot(reason);
}

void SystemOperationServiceImpl::ResetDevice(const std::string& reason) {
    auto base_dir = cosmo::path::GetBaseDir();
    reboot_mgr_.Reset(reason, base_dir);
}

cosmo::util::ErrorEnum SystemOperationServiceImpl::ExportLogs(std::string& fileName, std::string& fileUrl) {
    // Limit peak snapshot storage when multiple users request an export.
    static std::mutex export_mutex;
    const std::lock_guard<std::mutex> lock(export_mutex);
    fileName.clear();
    fileUrl.clear();
    auto timestamp      = cosmo::util::GetMilliseconds();
    auto web_local_path = cosmo::path::GetWebLocalPath(timestamp);
    std::error_code filesystem_error;
    for (fs::directory_iterator entry(web_local_path, filesystem_error), end;
         !filesystem_error && entry != end; entry.increment(filesystem_error)) {
        const auto& path = entry->path();
        if (path.filename().string().find("IedLog") == 0 && path.extension() == ".tar") {
            // Allow time to download an earlier result before reclaiming it.
            std::error_code time_error;
            const auto modified = fs::last_write_time(path, time_error);
            if (time_error || fs::file_time_type::clock::now() - modified < std::chrono::hours(1)) {
                continue;
            }
            LOG_INFO("remove {}", path);
            std::error_code remove_error;
            fs::remove(path, remove_error);
        }
    }
    if (filesystem_error) {
        LOG_WARN("Failed to enumerate export directory {}: {}", web_local_path, filesystem_error.message());
        return cosmo::util::ErrorEnum::SysErr;
    }

    const auto archive_name =
        "IedLog" + std::to_string(timestamp) + "_" + cosmo::util::GenerateUUID() + ".tar";
    const auto save_path   = fs::path(web_local_path) / archive_name;
    const auto log_path    = fs::path(cosmo::path::GetLogPath()).lexically_normal();
    const auto config_path = fs::path(cosmo::path::GetCfgPath()).lexically_normal();
    if (!fs::is_directory(log_path, filesystem_error) || filesystem_error ||
        !fs::is_directory(config_path, filesystem_error) || filesystem_error) {
        LOG_ERRO("{}", "Cannot export invalid log/config directories");
        return cosmo::util::ErrorEnum::SysErr;
    }

    std::string error;
    if (!CreateLogArchive(log_path, config_path, cosmo::path::GetTemporaryDirPath(), save_path, error)) {
        LOG_ERRO("Log export failed: {}", error);
        return cosmo::util::ErrorEnum::SysErr;
    }

    fileName = archive_name;
    fileUrl  = (fs::path(cosmo::path::GetWebAcessPath(timestamp)) / fileName).string();
    return {};
}

cosmo::util::ErrorEnum SystemOperationServiceImpl::Upgrade(const std::string& filePath) {
    auto result = cosmo::PacketUpgrade(filePath);
    if (result == cosmo::util::ErrorEnum::Success) {
        reboot_mgr_.Reboot("upgrade Reboot");
    }
    return result;
}

cosmo::util::ErrorEnum SystemOperationServiceImpl::CheckUpgradeSpace(std::uint64_t packageSizeBytes,
                                                                     bool cleanupEventMedia,
                                                                     UpgradeSpaceStatus& status) {
    return cosmo::service::CheckUpgradeStorage(cosmo::path::GetBaseDir(),
                                               cosmo::path::GetEventRootPath(false), packageSizeBytes,
                                               cleanupEventMedia, status);
}

void SystemOperationServiceImpl::ShowThreadDebugInfo() {
    cosmo::util::ShowAllThreads();
}

}  // namespace cosmo::service
