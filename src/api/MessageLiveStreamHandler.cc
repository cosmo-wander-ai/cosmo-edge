// MessageLiveStreamHandler — Message Live Stream Handler implementation.

#include "api/MessageLiveStreamHandler.h"

#include "service/detail/ServiceRegistry.h"
#include "service/media/ILiveStreamService.h"
#include "util/ErrorCode.h"
#include "util/Log.h"

namespace cosmo {
MessageLiveStreamHandler::MessageLiveStreamHandler(service::ILiveStreamService& live_stream_service)
    : live_stream_service_(live_stream_service) {}

LiveStream::MsgRequestLiveStreamSend MessageLiveStreamHandler::Handle(
    LiveStream::MsgRequestLiveStreamRecv&& data, std::error_condition& errc) {
    LiveStream::MsgRequestLiveStreamSend retData{};
    errc = live_stream_service_.ViewerCreate(data.channelId, data.algorithmId, retData.resData.stream);
    return retData;
}

LiveStream::MsgStreamKeepAliveSend MessageLiveStreamHandler::Handle(LiveStream::MsgStreamKeepAliveRecv&& data,
                                                                    std::error_condition& errc) {
    LiveStream::MsgStreamKeepAliveSend retData{};
    errc = live_stream_service_.ViewerHeartBeat(data.channelId, data.algorithmId);
    return retData;
}

LiveStream::MsgStreamStopSend MessageLiveStreamHandler::Handle(LiveStream::MsgStreamStopRecv&& data,
                                                               std::error_condition& /*errc*/) {
    LiveStream::MsgStreamStopSend retData{};
    live_stream_service_.ViewerDelete(data.channelId, data.algorithmId);

    return retData;
}

}  // namespace cosmo
