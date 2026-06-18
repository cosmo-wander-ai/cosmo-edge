// LiveStream DTO definitions (extracted from MessageLiveStreamHandler.h)

#pragma once

#include <system_error>

#include "util/dto/ServerMsgTypes.h"

namespace cosmo {
namespace LiveStream {
    struct MsgRequestLiveStreamRecv : public MsgRecvHead {
        std::string channelId;
        std::string algorithmId;
    };

    void to_json(nlohmann::json& j, const MsgRequestLiveStreamRecv& v);
    void from_json(const nlohmann::json& j, MsgRequestLiveStreamRecv& v);

    // Stream info for live stream response
    struct LiveStreamInfo {
        std::string protocol{"rtmp"};
        std::string url;
        std::string webrtcUrl;
        std::string flvUrl;
        std::string hlsUrl;
        int keepAliveInterval;
        std::string keepAliveUrl;
        int port{8081};
        int httpPort{8080};
        int rtcApiPort{1985};
        int portMin{8081};
        int portMax{8088};
        friend void to_json(nlohmann::json& j, const LiveStreamInfo& v);
        friend void from_json(const nlohmann::json& j, LiveStreamInfo& v);
    };

    struct LiveStreamResData {
        LiveStreamInfo stream;
        friend void to_json(nlohmann::json& j, const LiveStreamResData& v);
        friend void from_json(const nlohmann::json& j, LiveStreamResData& v);
    };

    //
    struct MsgRequestLiveStreamSend : public MsgSendHead {
        LiveStreamResData resData;
    };

    void to_json(nlohmann::json& j, const MsgRequestLiveStreamSend& v);
    void from_json(const nlohmann::json& j, MsgRequestLiveStreamSend& v);

    // Video heartbeat request
    struct MsgStreamKeepAliveRecv : public MsgRecvHead {
        std::string channelId;
        std::string algorithmId;
    };

    void to_json(nlohmann::json& j, const MsgStreamKeepAliveRecv& v);
    void from_json(const nlohmann::json& j, MsgStreamKeepAliveRecv& v);

    // Video heartbeat response
    struct MsgStreamKeepAliveSend : public MsgSendHead {};

    struct MsgStreamStopRecv : public MsgRecvHead {
        std::string channelId;
        std::string algorithmId;
    };

    void to_json(nlohmann::json& j, const MsgStreamStopRecv& v);
    void from_json(const nlohmann::json& j, MsgStreamStopRecv& v);

    // Video heartbeat response
    struct MsgStreamStopSend : public MsgSendHead {};
}  // namespace LiveStream
}  // namespace cosmo
