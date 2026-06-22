/// @file IEventNotifier.h
/// @brief Event notifier interface — abstracts WebSocket event push and
///        client message handling so that flow modules don't depend on
///        the network layer directly.
#pragma once

#include "service/detail/ServiceRegistry.h"
#include "service/event/dto/ClientMsgCollect.h"
#include "service/event/dto/ClientMsgInfo.h"
#include "service/network/dto/ClientMsgTask.h"
#include "service/network/dto/ClientMsgVideo.h"
#include "util/AsyncQueue.h"
#include "util/dto/ClientMsgEvent.h"
#include "util/dto/ServerMsgTypes.h"

namespace cosmo::service {

/// Manages the WebSocket connection for real-time event push to clients
/// and provides queue-based async event delivery for alarms, face events,
/// and data collection reports.
class IEventNotifier {
public:
    virtual ~IEventNotifier() = default;

    // ── Server Lifecycle ──

    /// Initialize the WebSocket server on the given host and port.
    /// @param hostIp IP address to bind.
    /// @param port   Port number to listen on.
    /// @return true if initialization succeeded.
    virtual bool InitializeWebSocket(const std::string& hostIp, int port) = 0;

    /// Shutdown the WebSocket server and release resources.
    virtual void ShutdownWebSocket() = 0;

    // ── Event Push ──

    /// Push an alarm event to all connected WebSocket clients.
    /// @param eventData Event data payload.
    virtual void WebSocketEventPush(cosmo::CMsgOnEventsReq& eventData) = 0;

    /// Notify the platform that a stream has completed.
    /// @param req  Completion request payload.
    /// @param rsp  [out] Completion response.
    /// @return true on success.
    virtual bool NotifyComplete(cosmo::CMsgOnCompleteReq& req, cosmo::CMsgOnCompleteRsp& rsp) = 0;

    /// Notify the platform of stream information updates.
    /// @param req  Info request payload.
    /// @param rsp  [out] Info response.
    /// @return true on success.
    virtual bool NotifyInfo(cosmo::CMsgOnInfoReq& req, cosmo::CMsgonInfoRsp& rsp) = 0;

    /// Get a video playback URL for a given stream.
    /// @param req  Playback URL request.
    /// @param rsp  [out] Playback URL response.
    /// @return true on success.
    virtual bool GetVideoPlayUrl(cosmo::CMsgGetVideoPlayReq& req, cosmo::CMsgGetVideoPlayRsp& rsp) = 0;

    // ── Queue Registration ──

    /// Register the async queue for alarm event delivery.
    /// @param que Reference to the alarm event queue.
    virtual void SetEventPostQue(cosmo::AsyncQueue<cosmo::CMsgOnEventsReq>& que) = 0;

    /// Register the async queue for data collection report delivery.
    /// @param que Reference to the collection report queue.
    virtual void SetCollectPostQue(cosmo::AsyncQueue<cosmo::CMsgCollectRptReq>& que) = 0;

    /// Register the async queue for face event delivery.
    /// @param que Reference to the face event queue.
    virtual void SetFaceEventPostQue(cosmo::AsyncQueue<cosmo::CMsgFaceEventReq>& que) = 0;

    // ── Queued Event Push ──

    /// Enqueue an alarm event for async delivery.
    /// @param msg Alarm event message.
    virtual void EventPush(cosmo::CMsgOnEventsReq& msg) = 0;

    /// Enqueue a face event for async delivery.
    /// @param msg Face event message.
    virtual void FaceEventPush(cosmo::CMsgFaceEventReq& msg) = 0;

    /// Enqueue a data collection report for async delivery.
    /// @param msg Collection report message.
    virtual void CollectPush(cosmo::CMsgCollectRptReq& msg) = 0;
};

}  // namespace cosmo::service
