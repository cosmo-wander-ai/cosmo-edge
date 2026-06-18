// Narrow interface for camera device CRUD operations.
// ISP split from ICameraService.
#pragma once

#include <string>
#include <vector>

#include "util/ErrorCode.h"
#include "util/dto/CameraMsgTypes.h"

namespace cosmo::service {

/// Create, read, update, and delete operations for camera device entries.
///
/// Manages the persistent camera registry.  Each camera maps to a physical
/// IP camera or USB device and holds its RTSP URL, credentials, and
/// associated algorithm channels.
class ICameraDeviceCrud {
public:
    virtual ~ICameraDeviceCrud() = default;

    /// Add a new camera device.
    /// @param config Camera configuration (populated on success with generated fields).
    /// @param id     [out] Generated camera ID.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum Add(cosmo::MsgCameraInfo& config, std::string& id) = 0;

    /// Update an existing camera device configuration.
    /// @param config Updated camera configuration (must contain a valid ID).
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum Update(cosmo::MsgCameraInfo& config) = 0;

    /// Delete a camera device by ID.
    /// @param cameraId Camera identifier to remove.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum Delete(const std::string& cameraId) = 0;

    /// Query camera devices with pagination and optional filters.
    /// @param channelName   Channel name filter (empty for all).
    /// @param channelStatus Status filter (-1 for all).
    /// @param pageNum       Page number (1-based).
    /// @param pageSize      Page size.
    /// @param total         [out] Total matching record count.
    /// @return Vector of matching camera device configurations.
    virtual std::vector<cosmo::MsgCameraInfo> Query(const std::string& channelName, int channelStatus,
                                                    int pageNum, int pageSize, size_t& total) = 0;
};

}  // namespace cosmo::service
