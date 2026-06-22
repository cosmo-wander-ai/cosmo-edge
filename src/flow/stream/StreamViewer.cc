// StreamViewer — Stream Viewer implementation.

#include "flow/stream/StreamViewer.h"

#include <cstdlib>

#include "util/EnvUtil.h"
#include "util/FormatString.h"
#include "util/Log.h"
#include "util/TimeUtil.h"
#include "util/dto/CameraMsgTypes.h"

namespace cosmo {

namespace {
    std::string BuildRtmpPushUrl(const std::string& channelId, const std::string& algId) {
        std::string base = util::GetEnvOrDefault("COSMO_STREAM_RTMP_BASE", "rtmp://127.0.0.1:1936/live");
        while (!base.empty() && base.back() == '/') {
            base.pop_back();
        }

        const std::string streamName = algId.empty() ? channelId : COSMO_FORMAT("{}_{}", channelId, algId);
        return COSMO_FORMAT("{}/{}", base, streamName);
    }

#ifdef COSMO_NN_USE_CPU_BACKEND
    bool IsEnabledEnv(const char* key) {
        const char* value = std::getenv(key);
        return value &&
               (std::string(value) == "1" || std::string(value) == "true" || std::string(value) == "TRUE" ||
                std::string(value) == "on" || std::string(value) == "ON");
    }
#endif
}  // namespace

StreamViewer::~StreamViewer() {
    if (data_packet_) {
        channel_inst_->RemoveViewerPacketQueue(async_packet_queue_);
    } else {
        channel_inst_->RemoveViewerFrameQueue(alg_id_);
    }

    LOG_INFO("{}/{} Delete", channel_id_, alg_id_);
}

StreamViewer::StreamViewer(AlgChannelPtr channelInst, const std::string& channelId, const std::string& algId)
    : channel_id_(channelId),
      alg_id_(algId),
      channel_inst_(channelInst),
      async_packet_queue_(
          std::make_shared<AsyncQueue<VideoPacketPtr>>("SourceViewerQueue " + channelId, 300)),
      async_frame_queue_("ReCodecViewerQueue " + channelId + "/" + algId, 30) {
    LOG_INFO("{}/{} Init", channel_id_, alg_id_);
    MsgCameraAttr attr;
    if (!channelInst->GetAttr(attr)) {
        LOG_WARN("{}/{} Init, But GetAttr Failed", channel_id_, alg_id_);
        return;
    }
    if (ChannelStatus::ChannelStatusOnline != attr.channelStatus) {
        LOG_WARN("{}/{} Init, But Channel Not Online", channel_id_, alg_id_);
        return;
    }
    if (!((attr.codec == "H265") || (attr.codec == "H264") || (attr.codec == "MJPEG"))) {
        LOG_WARN("{}/{} Init, But Channel Codec {} Not Support", channel_id_, alg_id_, attr.codec);
        return;
    }
    if (attr.fps < 1.0f) {
        LOG_WARN("{}/{} Init, But Channel fps {} Not Support", channel_id_, alg_id_, attr.fps);
        return;
    }
    std::string url;
    if (algId.empty()) {
        ctrl_fps_ = attr.fps;
    } else {
        // Algo preview: lower fps to keep OSD+encode stable under heavy inference load.
        ctrl_fps_ = 10.0;
    }
    url = BuildRtmpPushUrl(channelId, algId);
    LOG_INFO("{}/{} RTMP push url: {}", channel_id_, alg_id_, url);

    out_fps_ctl_.ChangeFps(attr.fps, ctrl_fps_);
    const bool shouldEncodeForPreview = !((algId.empty()) && (attr.codec == "H264"));
    // The RTMP pusher receives either raw H264 packets or H264 output from StreamViewerEncoder.
    const auto pushCodecType = media::VideoCodecType::kH264;

    if ((attr.width * attr.height) > (media::kVideoDefaultWidth * media::kVideoDefaultHeight)) {
        video_pusher_ =
            std::make_shared<RtmpStreamPusher>(media::VideoCodecType::kH264, url, media::kVideoDefaultWidth,
                                               media::kVideoDefaultHeight, ctrl_fps_);
    } else {
        video_pusher_ =
            std::make_shared<RtmpStreamPusher>(pushCodecType, url, attr.width, attr.height, ctrl_fps_);
    }

    if (shouldEncodeForPreview) {
        if ((attr.width * attr.height) > (media::kVideoDefaultWidth * media::kVideoDefaultHeight)) {
            encoder_ =
                std::make_shared<StreamViewerEncoder>(media::VideoCodecType::kH264, media::kVideoDefaultWidth,
                                                      media::kVideoDefaultHeight, video_pusher_);
        } else {
            encoder_ = std::make_shared<StreamViewerEncoder>(media::VideoCodecType::kH264, attr.width,
                                                             attr.height, video_pusher_);
        }
    }
    async_packet_queue_->SetProcessor([this](VideoPacketPtr&& data) { HandlePacket(std::move(data)); });

    async_frame_queue_.SetProcessor([this](VideoFramePtr&& data) { HandleFrame(std::move(data)); });

    // Channel preview, no overlay
    if (algId.empty()) {
        if (attr.codec == "H264") {
            data_packet_ = true;
            channelInst->AddViewerPacketQueue(async_packet_queue_);
        } else {
            channelInst->AddViewerFrameQueue(alg_id_, async_frame_queue_);
        }
    } else  // Task preview, overlay
    {
        if (EncoderReady()) {
            data_overview_ = true;
            overviewer_    = std::make_shared<StreamViewerOverview>(channel_id_, alg_id_);
            channelInst->AddViewerFrameQueue(alg_id_, async_frame_queue_);
#ifdef COSMO_NN_USE_CPU_BACKEND
        } else if (attr.codec == "H264" && IsEnabledEnv("COSMO_CPU_OVERLAY_RAW_FALLBACK")) {
            LOG_WARN("{}/{} overlay encoder unavailable, fallback to raw H264 preview by env", channel_id_,
                     alg_id_);
            data_packet_ = true;
            channelInst->AddViewerPacketQueue(async_packet_queue_);
#endif
        } else {
            LOG_WARN("{}/{} overlay encoder unavailable and codec {} cannot fallback to raw preview",
                     channel_id_, alg_id_, attr.codec);
        }
    }
}
void StreamViewer::HeartBeat() {
    heartbeat_timestamp_ = util::GetMilliseconds();
    std::string logInfo;

    if (overviewer_) {
        auto overviewerDebudInfo = overviewer_->GetProcInfo();
        std::string tmp          = COSMO_FORMAT("Overlay-- Input:{} frames Output:{} frames",
                                                overviewerDebudInfo.recvFrames, overviewerDebudInfo.sendFrames);
        logInfo                  = logInfo + tmp;
    }
    if (encoder_) {
        auto encodeDebudInfo = encoder_->GetProcInfo();
        std::string tmp      = COSMO_FORMAT(" Encode-- Input:{} frames Output:{} frames",
                                            encodeDebudInfo.recvFrames, encodeDebudInfo.sendFrames);
        logInfo              = logInfo + tmp;
    }
    if (video_pusher_) {
        auto videoPusherDebudInfo = video_pusher_->GetProcInfo();
        std::string tmp           = COSMO_FORMAT(" Push-- Input:{} frames Output:{} frames",
                                                 videoPusherDebudInfo.recvFrames, videoPusherDebudInfo.sendFrames);
        logInfo                   = logInfo + tmp;
    }
    LOG_INFO("{}/{} Overview: {}", channel_id_, alg_id_, logInfo);
}

bool StreamViewer::HaveEncoder() {
    return EncoderReady();
}

bool StreamViewer::EncoderReady() const {
    return encoder_ && encoder_->IsOpened();
}

bool StreamViewer::HeartBeatCheck() {
    auto now = util::GetMilliseconds();
    if (abs(now - heartbeat_timestamp_) > heartbeat_duration_) {
        heartbeat_failed_count_++;
    } else {
        heartbeat_failed_count_ = 0;
    }
    return (heartbeat_failed_count_ >= heartbeat_failed_interval_);
}

void StreamViewer::HandlePacket(VideoPacketPtr frame) {
    video_pusher_->SetVideoAttr(frame->GetWidth(), frame->GetHeight(), frame->GetFPS());
    video_pusher_->PushFrame(frame->GetData(), frame->GetSize());
}

void StreamViewer::UpdateCtrlFps() {
    // Update every 100 frames, can be changed to timer update later
    if ((data_index_ < 200) || (0 == data_index_ % 100)) {
        double infps   = in_fps_;
        double absDiff = std::abs(infps - out_fps_ctl_.GetRealFps());
        if (absDiff > 0.1) {
            out_fps_ctl_.SetRealFps(in_fps_);
        }
    }
}

void StreamViewer::HandleFrame(VideoFramePtr frame) {
    in_fps_ = input_fps_calc_.Fps();
    data_index_++;
    UpdateCtrlFps();
    if (!VideoFrameValid(frame)) {
        return;
    }
    if (out_fps_ctl_.IsFilter(data_index_))  // Frame rate filtering
    {
        return;
    }

    if (overviewer_) {
        if (encoder_) {
            overviewer_->SetEncodeDuration(encoder_->GetDurationInfo());
        }
        auto outFrame = overviewer_->HandFrame(frame);
        if (VideoFrameValid(outFrame) && EncoderReady()) {
            encoder_->HandFrame(outFrame);
        }
        return;
    }
    // Encoding
    if (EncoderReady()) {
        encoder_->HandFrame(frame);
    }

    return;
}
}  // namespace cosmo
