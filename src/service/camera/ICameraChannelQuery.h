// Narrow interface for camera channel instance access, image encoding,
// web path utilities, and USB camera enumeration.
// ISP split from ICameraService.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "service/camera/dto/CameraDto.h"
#include "service/media/dto/VideoFrameFwd.h"

namespace cosmo {
class AlgChannel;
using AlgChannelPtr = std::shared_ptr<AlgChannel>;
}  // namespace cosmo

namespace cosmo::service {

/// Read-only access to camera channel instances, image encoding helpers,
/// web path generation, and USB camera enumeration.
///
/// Consumers that only need to read channel data or encode frames should
/// depend on this narrow interface rather than the full ICameraService.
class ICameraChannelQuery {
public:
    virtual ~ICameraChannelQuery() = default;

    // ── Channel Instance Access ──

    /// Retrieve the AlgChannel instance for a given channel ID.
    /// @param channelId Unique channel identifier.
    /// @return Shared pointer to the channel, or nullptr if not found.
    virtual cosmo::AlgChannelPtr GetChannelInst(const std::string& channelId) = 0;

    /// Get the human-readable name of a channel.
    /// @param channelId Unique channel identifier.
    /// @return Channel name string, or empty if not found.
    virtual std::string GetChannelName(const std::string& channelId) const = 0;

    /// Initialize camera entities from persisted configuration.
    virtual void InitCameraEntities() = 0;

    // ── Image Encoding and Path Utilities ──

    /// Check whether the device operates in IoT network mode.
    /// @return true if IoT (platform-connected) mode is active.
    virtual bool IsIotNetworkMode() = 0;

    /// Encode a video frame to JPEG format.
    /// @param frame Source video frame.
    /// @return JPEG-encoded byte buffer.
    virtual std::vector<uint8_t> EncodeJpeg(const VideoFramePtr& frame) = 0;

    /// Get the local filesystem path for web-accessible temporary files.
    /// @param timestamp Optional timestamp for time-partitioned directories.
    /// @return Absolute local path.
    virtual std::string GetWebLocalPath(int64_t timestamp = 0) = 0;

    /// Get the web-accessible URL path for temporary files.
    /// @param timestamp Optional timestamp for time-partitioned directories.
    /// @return Relative web URL path.
    virtual std::string GetWebAccessPath(int64_t timestamp = 0) = 0;

    // ── USB Camera ──

    /// Enumerate connected USB camera devices.
    /// @return List of detected USB camera device descriptors.
    virtual std::vector<cosmo::camera::MsgUsbCameraDevice> QueryUsbCameraList() = 0;
};

}  // namespace cosmo::service
