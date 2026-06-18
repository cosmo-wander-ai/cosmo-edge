/// @file ILiveStreamService.h
/// @brief Live stream service interface — manages RTSP-to-WebSocket live
///        preview sessions for camera channels.
#pragma once

#include <string>
#include <system_error>

#include "service/detail/ServiceRegistry.h"
#include "service/media/dto/LiveStreamDto.h"
#include "util/ErrorCode.h"

namespace cosmo::service {

/// Controls live video preview sessions that transcode RTSP streams
/// to WebSocket-delivered H.264/MJPEG for browser-based viewing.
///
/// Each viewer session is identified by (channelId, algCode) and requires
/// periodic heartbeat to stay alive.
class ILiveStreamService {
public:
    virtual ~ILiveStreamService() = default;

    /// Create a new live viewer session and populate stream connection info.
    /// @param channelId   Camera channel identifier.
    /// @param algCode     Algorithm code for the overlay stream (empty for raw).
    /// @param streamInfo  [out] Populated with protocol, URLs, ports, keepalive config.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum ViewerCreate(const std::string& channelId, const std::string& algCode,
                                                LiveStream::LiveStreamInfo& streamInfo) = 0;

    /// Delete a live viewer session.
    /// @param channelId Camera channel identifier.
    /// @param algCode   Algorithm code of the session.
    /// @return true on success.
    virtual bool ViewerDelete(const std::string& channelId, const std::string& algCode) = 0;

    /// Send a heartbeat for an active viewer session to prevent timeout.
    /// @param channelId Camera channel identifier.
    /// @param algCode   Algorithm code of the session.
    /// @return ErrorEnum::kSuccess if the session is still alive.
    virtual cosmo::util::ErrorEnum ViewerHeartBeat(const std::string& channelId,
                                                   const std::string& algCode) = 0;

    // ── Preview Channel Control ──

    /// Set the maximum number of concurrent preview channels.
    /// @param viewNum Maximum viewer count.
    virtual void SetViewCounts(int viewNum) = 0;
};

// Dependency injection

}  // namespace cosmo::service
