#pragma once

#include <string>

namespace cosmo::service::camera {

struct CameraTaskDto {
    std::string taskId;
    std::string algorithmCode;  // Algorithm code
    std::string algorithmName;  // Algorithm code
    std::string scheduleId;     // Schedule template
    std::string scheduleName;   // Schedule template
    bool ready{false};
    std::string runtimeState{"unknown"};
    bool enable{false};  // Enable switch
};

}  // namespace cosmo::service::camera
