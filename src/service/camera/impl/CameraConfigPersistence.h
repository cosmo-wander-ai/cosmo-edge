#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "service/camera/impl/CameraServiceImpl.h"

namespace cosmo::service::detail {

// Persistence utility for camera configuration.
class CameraConfigPersistence {
public:
    // Load serialized camera entity configurations from a local JSON file.
    static std::vector<CameraEntityPtr> LoadConfig(const std::string& conf_file_path,
                                                   const std::string& conf_file_name);

    // Save a snapshot of the current camera configuration to a local file.
    static void SaveConfig(const std::string& conf_file_path, const std::string& conf_file_name,
                           const std::vector<CameraEntityPtr>& snapshot);

    // Clean up redundant abandoned configuration files that exist on disk
    // but are not in the active_cameras list.
    static void RemoveDiscardedConfigs(const std::string& conf_file_path,
                                       const std::vector<CameraEntityPtr>& active_cameras);

    // Extract the numeric ID from the camera channel ID string.
    static cosmo::util::ErrorEnum ExtractCameraNumber(const std::string& str, int* max_number);

private:
    static std::mutex file_mutex_;
};

}  // namespace cosmo::service::detail
