// CameraConfigPersistence — Persistence utility for camera configuration.

#include "service/camera/impl/CameraConfigPersistence.h"

#include <algorithm>
#include <filesystem>
#include <list>

#include "service/detail/ServiceRegistry.h"
#include "util/FileUtil.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"

namespace cosmo::service::detail {

std::mutex CameraConfigPersistence::file_mutex_;

cosmo::util::ErrorEnum CameraConfigPersistence::ExtractCameraNumber(const std::string& str, int* max_number) {
    if (!max_number) {
        return cosmo::util::ErrorEnum::InvalidParam;
    }
    if (str.size() < 2) {
        return cosmo::util::ErrorEnum::InvalidParam;
    }
    char first_char = str[0];
    if (first_char >= 'A' && first_char <= 'Z') {
        std::string number_str = str.substr(2);
        try {
            *max_number = std::stoi(number_str);
        } catch (const std::exception& e) {
            *max_number = 0;
        }
    } else {
        *max_number = 0;
    }
    return cosmo::util::ErrorEnum::Success;
}

std::vector<CameraEntityPtr> CameraConfigPersistence::LoadConfig(const std::string& conf_file_path,
                                                                 const std::string& conf_file_name) {
    std::vector<CameraEntityPtr> cameras;
    auto cfg_path =
        (std::filesystem::path(cosmo::path::GetCfgPath(conf_file_path)) / conf_file_name).string();
    if (!cosmo::util::LoadStructFromJsonFile(cfg_path, cameras)) {
        LOG_WARN("Failed to load camera config from {}", cfg_path);
    }
    return cameras;
}

void CameraConfigPersistence::SaveConfig(const std::string& conf_file_path, const std::string& conf_file_name,
                                         const std::vector<CameraEntityPtr>& snapshot) {
    LOG_INFO("{}", "CameraConfigPersistence: Saving File Begin");
    std::lock_guard<std::mutex> file_lock(file_mutex_);

    auto path = (std::filesystem::path(cosmo::path::GetCfgPath(conf_file_path)) / conf_file_name).string();
    if (!cosmo::util::SaveStructToJsonFile(path, snapshot)) {
        LOG_WARN("Failed to save camera config to {}", path);
    }
    LOG_INFO("{}", "CameraConfigPersistence: Saving File End");
}

void CameraConfigPersistence::RemoveDiscardedConfigs(const std::string& conf_file_path,
                                                     const std::vector<CameraEntityPtr>& active_cameras) {
    std::string filter;
    auto path       = cosmo::path::GetCfgPath(conf_file_path);
    auto cfg_b_path = cosmo::path::GetBackupCfgPath(conf_file_path);

    auto files = cosmo::util::GetAllFileName(path, filter, cosmo::util::FileAttr::FileAttrDir);

    for (const auto& file : files) {
        auto it = std::find_if(active_cameras.begin(), active_cameras.end(),
                               [&](const CameraEntityPtr& cfg) { return cfg->videoChannelId == file; });

        if (it == active_cameras.end()) {
            std::string camera_path   = (std::filesystem::path(path) / file).string();
            std::string camera_b_path = (std::filesystem::path(cfg_b_path) / file).string();
            cosmo::util::RemovePath(camera_path);
            cosmo::util::RemovePath(camera_b_path);
            LOG_INFO("CameraConfigPersistence Rmv: {}", camera_path);
        }
    }
}

}  // namespace cosmo::service::detail
