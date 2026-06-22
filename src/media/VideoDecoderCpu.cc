// VideoDecoderCpu — Video Decoder Cpu implementation.

#include "media/VideoDecoderCpu.h"

#include <cstring>

#ifdef __cplusplus
extern "C" {
#endif
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"
#ifdef __cplusplus
}
#endif

#include "media/PixelFormatUtils.h"
#include "util/Log.h"

namespace cosmo {
namespace media {

    VideoDecoderCpu::VideoDecoderCpu(size_t name) : VideoDecoder(name) {}

    VideoDecoderCpu::~VideoDecoderCpu() {
        VideoDecoderCpu::Close();
    }

    bool VideoDecoderCpu::IsOpened() {
        return opened_;
    }

    bool VideoDecoderCpu::Open() {
        AVCodecID codec_id = AV_CODEC_ID_NONE;
        if (codec_type_ == VideoCodecType::kH265) {
            codec_id = AV_CODEC_ID_HEVC;
        } else if (codec_type_ == VideoCodecType::kH264) {
            codec_id = AV_CODEC_ID_H264;
        } else {
            LOG_WARN("{} OpenDecoder unsupported codec type ({})", idx_name_, static_cast<int>(codec_type_));
            return false;
        }

        auto* codec = avcodec_find_decoder(codec_id);
        if (!codec) {
            LOG_WARN("{} avcodec_find_decoder failed for codec_id {}", idx_name_, static_cast<int>(codec_id));
            return false;
        }

        codec_ctx_ = avcodec_alloc_context3(codec);
        if (!codec_ctx_) {
            LOG_WARN("{} avcodec_alloc_context3 failed", idx_name_);
            return false;
        }

        // Use single-threaded decoding — simpler and avoids concurrency issues
        codec_ctx_->thread_count = 1;

        if (avcodec_open2(codec_ctx_, codec, nullptr) < 0) {
            LOG_WARN("{} avcodec_open2 failed", idx_name_);
            avcodec_free_context(&codec_ctx_);
            return false;
        }

        av_frame_  = av_frame_alloc();
        av_packet_ = av_packet_alloc();
        if (!av_frame_ || !av_packet_) {
            LOG_WARN("{} av_frame_alloc/av_packet_alloc failed", idx_name_);
            Close();
            return false;
        }

        opened_ = true;
        LOG_INFO("{} CPU decoder opened (codec_id={})", idx_name_, static_cast<int>(codec_id));
        return true;
    }

    bool VideoDecoderCpu::Close() {
        if (!opened_)
            return true;

        opened_ = false;

        if (av_frame_) {
            av_frame_free(&av_frame_);
        }
        if (av_packet_) {
            av_packet_free(&av_packet_);
        }
        if (codec_ctx_) {
            avcodec_free_context(&codec_ctx_);
        }

        LOG_INFO("{} CPU decoder closed", idx_name_);
        return true;
    }

    bool VideoDecoderCpu::SendPacket(const uint8_t* pkt, size_t len, int64_t frame_idx) {
        if (!opened_ || !codec_ctx_)
            return false;

        av_packet_->data = const_cast<uint8_t*>(pkt);  // FFmpeg C API boundary
        av_packet_->size = static_cast<int>(len);
        av_packet_->dts  = frame_idx;
        av_packet_->pts  = frame_idx;

        int ret = avcodec_send_packet(codec_ctx_, av_packet_);
        if (ret < 0) {
            LOG_WARN("{} avcodec_send_packet failed: {}", idx_name_, ret);
            return false;
        }
        return true;
    }

    VideoFramePtr VideoDecoderCpu::GetFrame() {
        if (!opened_ || !codec_ctx_)
            return nullptr;

        int ret = avcodec_receive_frame(codec_ctx_, av_frame_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return nullptr;
        }
        if (ret < 0) {
            LOG_WARN("{} avcodec_receive_frame failed: {}", idx_name_, ret);
            return nullptr;
        }

        width_  = static_cast<size_t>(av_frame_->width);
        height_ = static_cast<size_t>(av_frame_->height);

        auto frame = std::make_shared<VideoFrame>(static_cast<int>(width_), static_cast<int>(height_),
                                                  PixelFormat::PIXEL_I420);
        if (!frame || !frame->Active()) {
            LOG_WARN("{} VideoFrame allocation failed", idx_name_);
            av_frame_unref(av_frame_);
            return nullptr;
        }

        if (!CopyAVFrameToVideoFrame(av_frame_, frame)) {
            av_frame_unref(av_frame_);
            return nullptr;
        }

        frame->SetFrameIndex(static_cast<uint64_t>(
            av_frame_->best_effort_timestamp >= 0 ? av_frame_->best_effort_timestamp : av_frame_->pkt_dts));
        av_frame_unref(av_frame_);
        return frame;
    }

    bool VideoDecoderCpu::CopyAVFrameToVideoFrame(AVFrame* src, VideoFramePtr dst) {
        auto* dst_data = dst->GetData();
        if (!dst_data) {
            LOG_WARN("{} VideoFrame has no backing memory", idx_name_);
            return false;
        }

        auto w = dst->GetWidth();
        auto h = dst->GetHeight();

        // If AVFrame is already YUV420P, direct copy
        if (src->format == AV_PIX_FMT_YUV420P) {
            // Y plane
            for (size_t row = 0; row < h; ++row) {
                std::memcpy(dst_data + row * w, src->data[0] + row * src->linesize[0], w);
            }
            // U plane
            size_t half_w = w / 2;
            size_t half_h = h / 2;
            auto* u_dst   = dst_data + w * h;
            for (size_t row = 0; row < half_h; ++row) {
                std::memcpy(u_dst + row * half_w, src->data[1] + row * src->linesize[1], half_w);
            }
            // V plane
            auto* v_dst = u_dst + half_w * half_h;
            for (size_t row = 0; row < half_h; ++row) {
                std::memcpy(v_dst + row * half_w, src->data[2] + row * src->linesize[2], half_w);
            }
        } else {
            // Convert to YUV420P via sws_scale
            auto* sws_ctx = sws_getContext(static_cast<int>(w), static_cast<int>(h),
                                           static_cast<AVPixelFormat>(src->format), static_cast<int>(w),
                                           static_cast<int>(h), AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr,
                                           nullptr, nullptr);
            if (!sws_ctx) {
                LOG_WARN("{} sws_getContext failed for format {}", idx_name_, src->format);
                return false;
            }

            uint8_t* dst_planes[3];
            int dst_strides[3];
            dst_planes[0]  = dst_data;
            dst_planes[1]  = dst_data + w * h;
            dst_planes[2]  = dst_planes[1] + w * h / 4;
            dst_strides[0] = static_cast<int>(w);
            dst_strides[1] = static_cast<int>(w / 2);
            dst_strides[2] = static_cast<int>(w / 2);

            sws_scale(sws_ctx, src->data, src->linesize, 0, static_cast<int>(h), dst_planes, dst_strides);
            sws_freeContext(sws_ctx);
        }
        return true;
    }

}  // namespace media
}  // namespace cosmo
