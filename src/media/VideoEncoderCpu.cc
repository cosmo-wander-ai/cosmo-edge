// VideoEncoderCpu — Video Encoder Cpu implementation.

#include "media/VideoEncoderCpu.h"

#include <cstring>
#include <string>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif
#include "libavutil/error.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#ifdef __cplusplus
}
#endif

#include "util/Log.h"

namespace cosmo {
namespace media {
    namespace {
        std::string AvErrorToString(int err) {
            char buf[AV_ERROR_MAX_STRING_SIZE]{};
            av_strerror(err, buf, sizeof(buf));
            return buf;
        }

        const AVCodec* FindEncoder(AVCodecID codecId) {
            if (codecId == AV_CODEC_ID_H264) {
                if (const AVCodec* codec = avcodec_find_encoder_by_name("libopenh264")) {
                    return codec;
                }

            } else if (codecId == AV_CODEC_ID_HEVC) {
                if (const AVCodec* codec = avcodec_find_encoder_by_name("libx265")) {
                    return codec;
                }
            }

            return avcodec_find_encoder(codecId);
        }

        struct H264NaluView {
            size_t offset{0};
            size_t size{0};
            uint8_t type{0};
        };

        size_t H264StartCodeSize(const uint8_t* data, size_t size, size_t pos) {
            if (pos + 3 <= size && data[pos] == 0x00 && data[pos + 1] == 0x00 && data[pos + 2] == 0x01) {
                return 3;
            }
            if (pos + 4 <= size && data[pos] == 0x00 && data[pos + 1] == 0x00 && data[pos + 2] == 0x00 &&
                data[pos + 3] == 0x01) {
                return 4;
            }
            return 0;
        }

        std::vector<H264NaluView> SplitH264NalUnits(const uint8_t* data, size_t size) {
            std::vector<H264NaluView> nalus;
            size_t pos = 0;
            while (pos + 3 < size) {
                size_t start_code_size = H264StartCodeSize(data, size, pos);
                if (start_code_size == 0) {
                    ++pos;
                    continue;
                }

                const size_t nalu_offset = pos;
                const size_t type_offset = pos + start_code_size;
                if (type_offset >= size) {
                    break;
                }
                size_t next = type_offset + 1;
                while (next < size && !(next + 3 < size && H264StartCodeSize(data, size, next) != 0)) {
                    ++next;
                }

                nalus.push_back(
                    {nalu_offset, next - nalu_offset, static_cast<uint8_t>(data[type_offset] & 0x1F)});
                pos = next;
            }
            return nalus;
        }

        std::vector<uint8_t> NormalizeH264ForRtmp(const uint8_t* data, size_t size) {
            auto nalus   = SplitH264NalUnits(data, size);
            bool has_idr = false;
            bool has_sps = false;
            bool has_pps = false;
            for (const auto& nalu : nalus) {
                has_idr = has_idr || nalu.type == 5;
                has_sps = has_sps || nalu.type == 7;
                has_pps = has_pps || nalu.type == 8;
            }

            if (!has_idr || !has_sps || !has_pps) {
                return {data, data + size};
            }

            std::vector<uint8_t> out;
            out.reserve(size);
            auto append_type = [&](uint8_t type) {
                for (const auto& nalu : nalus) {
                    if (nalu.type == type) {
                        out.insert(out.end(), data + nalu.offset, data + nalu.offset + nalu.size);
                    }
                }
            };

            append_type(7);  // SPS
            append_type(8);  // PPS
            for (const auto& nalu : nalus) {
                if (nalu.type != 7 && nalu.type != 8 && nalu.type != 9) {
                    out.insert(out.end(), data + nalu.offset, data + nalu.offset + nalu.size);
                }
            }
            return out.empty() ? std::vector<uint8_t>{data, data + size} : out;
        }
    }  // namespace

    VideoEncoderCpu::VideoEncoderCpu() : VideoEncoder() {}

    VideoEncoderCpu::~VideoEncoderCpu() {
        Clean();
    }

    bool VideoEncoderCpu::Open() {
        AVCodecID codec_id = AV_CODEC_ID_NONE;
        if (codec_type_ == VideoCodecType::kH264) {
            codec_id = AV_CODEC_ID_H264;
        } else if (codec_type_ == VideoCodecType::kH265) {
            codec_id = AV_CODEC_ID_HEVC;
        } else {
            LOG_WARN("CPU encoder: unsupported codec type {}", static_cast<int>(codec_type_));
            return false;
        }

        auto* codec = FindEncoder(codec_id);
        if (!codec) {
            // H.265 encoder may not be available in all FFmpeg builds
            LOG_WARN("CPU encoder: avcodec_find_encoder failed for codec_id {}", static_cast<int>(codec_id));
            return false;
        }

        codec_ctx_ = avcodec_alloc_context3(codec);
        if (!codec_ctx_) {
            LOG_WARN("{}", "CPU encoder: avcodec_alloc_context3 failed");
            return false;
        }

        codec_ctx_->width     = static_cast<int>(width_);
        codec_ctx_->height    = static_cast<int>(height_);
        codec_ctx_->pix_fmt   = AV_PIX_FMT_YUV420P;
        codec_ctx_->time_base = {1, 25};
        codec_ctx_->framerate = {25, 1};
        codec_ctx_->bit_rate  = 4000000;

        // GOP settings: all P-frames after first I-frame
        codec_ctx_->gop_size     = 25;
        codec_ctx_->max_b_frames = 0;

        // Quality setting via qmin/qmax for CQP-like behavior
        codec_ctx_->qmin = 28;
        codec_ctx_->qmax = 36;

        // RTMP preview pipeline parses Annex-B packets to collect SPS/PPS before writing FLV headers.
        // Keep headers in-band for CPU H264 so it behaves like demuxed camera packets.
        av_opt_set(codec_ctx_->priv_data, "preset", "fast", 0);
        av_opt_set(codec_ctx_->priv_data, "tune", "zerolatency", 0);

        int ret = avcodec_open2(codec_ctx_, codec, nullptr);
        if (ret < 0) {
            LOG_WARN("CPU encoder: avcodec_open2 failed codec:{} id:{} error:{} ({})", codec->name,
                     static_cast<int>(codec_id), ret, AvErrorToString(ret));
            avcodec_free_context(&codec_ctx_);
            return false;
        }

        av_frame_ = av_frame_alloc();
        if (!av_frame_) {
            LOG_WARN("{}", "CPU encoder: av_frame_alloc failed");
            Clean();
            return false;
        }
        av_frame_->format = AV_PIX_FMT_YUV420P;
        av_frame_->width  = codec_ctx_->width;
        av_frame_->height = codec_ctx_->height;

        if (av_frame_get_buffer(av_frame_, 0) < 0) {
            LOG_WARN("{}", "CPU encoder: av_frame_get_buffer failed");
            Clean();
            return false;
        }

        av_packet_ = av_packet_alloc();
        if (!av_packet_) {
            LOG_WARN("{}", "CPU encoder: av_packet_alloc failed");
            Clean();
            return false;
        }

        frame_pts_ = 0;
        closed_    = false;
        LOG_INFO("CPU encoder opened ({}x{}, codec_id={})", width_, height_, static_cast<int>(codec_id));
        return true;
    }

    VideoPacketPtr VideoEncoderCpu::SendYUVFrame(void* data) {
        if (closed_ || !codec_ctx_) {
            LOG_WARN("{}", "CPU encoder: not open");
            return nullptr;
        }

        if (!data) {
            LOG_WARN("{}", "CPU encoder: null data");
            return nullptr;
        }

        // Make frame writable (detach from buffer pool reference)
        if (av_frame_make_writable(av_frame_) < 0) {
            LOG_WARN("{}", "CPU encoder: av_frame_make_writable failed");
            return nullptr;
        }

        // CPU backend: data is raw uint8_t* pointing to YUV420P in host memory
        auto* src      = static_cast<uint8_t*>(data);
        size_t y_size  = width_ * height_;
        size_t uv_size = y_size / 4;

        // Copy Y plane
        for (size_t row = 0; row < height_; ++row) {
            std::memcpy(av_frame_->data[0] + row * av_frame_->linesize[0], src + row * width_, width_);
        }
        // Copy U plane
        auto half_h = height_ / 2;
        auto half_w = width_ / 2;
        for (size_t row = 0; row < half_h; ++row) {
            std::memcpy(av_frame_->data[1] + row * av_frame_->linesize[1], src + y_size + row * half_w,
                        half_w);
        }
        // Copy V plane
        for (size_t row = 0; row < half_h; ++row) {
            std::memcpy(av_frame_->data[2] + row * av_frame_->linesize[2],
                        src + y_size + uv_size + row * half_w, half_w);
        }

        av_frame_->pts = frame_pts_++;

        int ret = avcodec_send_frame(codec_ctx_, av_frame_);
        if (ret < 0) {
            LOG_WARN("CPU encoder: avcodec_send_frame failed: {}", ret);
            return nullptr;
        }

        ret = avcodec_receive_packet(codec_ctx_, av_packet_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return nullptr;
        }
        if (ret < 0) {
            LOG_WARN("CPU encoder: avcodec_receive_packet failed: {}", ret);
            return nullptr;
        }

        auto pkt = std::make_shared<VideoPacket>();
        if (codec_type_ == VideoCodecType::kH264) {
            pkt->data = NormalizeH264ForRtmp(av_packet_->data, av_packet_->size);
        } else {
            pkt->data = std::vector<uint8_t>(av_packet_->data, av_packet_->data + av_packet_->size);
        }

        av_packet_unref(av_packet_);
        return pkt;
    }

    void VideoEncoderCpu::Clean() {
        closed_ = true;

        if (av_packet_) {
            av_packet_free(&av_packet_);
        }
        if (av_frame_) {
            av_frame_free(&av_frame_);
        }
        if (codec_ctx_) {
            avcodec_free_context(&codec_ctx_);
        }
    }

}  // namespace media
}  // namespace cosmo
