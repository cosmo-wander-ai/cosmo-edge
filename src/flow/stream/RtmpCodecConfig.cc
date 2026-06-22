// RtmpCodecConfig — Rtmp Codec Config implementation.

#include "flow/stream/RtmpCodecConfig.h"

#include "util/Log.h"

namespace cosmo {

namespace {
    // Bitrate tiers by pixel count
    constexpr int kPixels1080p = 1920 * 1080;
    constexpr int kPixels720p  = 1280 * 720;

    constexpr int64_t kBitrate1080p = 4000000;  // 4 Mbps
    constexpr int64_t kBitrate720p  = 2000000;  // 2 Mbps
    constexpr int64_t kBitrate480p  = 1000000;  // 1 Mbps
}  // namespace

RtmpCodecConfig::RtmpCodecConfig(media::VideoCodecType codec_type, int width, int height, float fps)
    : codec_type_(codec_type), width_(width), height_(height), fps_(fps) {}

AVCodecID RtmpCodecConfig::ToAvCodecId(media::VideoCodecType type) {
    switch (type) {
        case media::VideoCodecType::kH264:
            return AV_CODEC_ID_H264;
        case media::VideoCodecType::kH265:
            return AV_CODEC_ID_H265;
        default:
            return AV_CODEC_ID_H264;
    }
}

int64_t RtmpCodecConfig::EstimateBitrate(int width, int height) {
    const int pixels = width * height;
    if (pixels >= kPixels1080p) {
        return kBitrate1080p;
    }
    if (pixels >= kPixels720p) {
        return kBitrate720p;
    }
    return kBitrate480p;
}

bool RtmpCodecConfig::ParseAndPrepare(const uint8_t* data, size_t size, const uint8_t*& out_data,
                                      size_t& out_size, media::HFrameType& main_type,
                                      media::HFrameType& lead_type) {
    // Extract VPS/SPS/PPS/SEI from NALU stream
    size_t offset            = 0;
    size_t sz                = 0;
    size_t last_frame_offset = 0;
    bool has_video_slice     = false;

    out_data  = data;
    out_size  = size;
    main_type = media::HFrameType::UNKNOWN;
    lead_type = media::HFrameType::UNKNOWN;

    if (!data || size == 0) {
        return false;
    }

    do {
        offset += sz;
        sz = media::SeparateHVideoFrame(data + offset, size - offset);

        auto assign = [](auto& v, const uint8_t* d, size_t off, size_t len) {
            if (v.empty()) {
                v.assign(d + off, d + off + len);
            }
        };

        auto type = media::GetFrameType(codec_type_, data + offset);
        switch (type) {
            case media::HFrameType::VPS:
                assign(vps_, data, offset, sz);
                break;
            case media::HFrameType::SPS:
                assign(sps_, data, offset, sz);
                break;
            case media::HFrameType::PPS:
                assign(pps_, data, offset, sz);
                break;
            case media::HFrameType::SEI:
                assign(sei_, data, offset, sz);
                break;
            case media::HFrameType::P:
            case media::HFrameType::I:
                last_frame_offset = offset;
                has_video_slice   = true;
                sz                = size - offset;
                break;
            default:
                break;
        }
    } while (offset + sz != size);

    lead_type = media::GetFrameType(codec_type_, data);
    if (!has_video_slice) {
        return false;
    }

    main_type = media::GetFrameType(codec_type_, data + last_frame_offset);

    // If I-frame lacks SPS prefix, prepend cached VPS/SPS/PPS/SEI
    if (main_type == media::HFrameType::I && lead_type != media::HFrameType::SPS &&
        lead_type != media::HFrameType::VPS) {
        prepend_buffer_.clear();
        prepend_buffer_.reserve(vps_.size() + sps_.size() + pps_.size() + sei_.size() + size);
        prepend_buffer_.insert(prepend_buffer_.end(), vps_.begin(), vps_.end());
        prepend_buffer_.insert(prepend_buffer_.end(), sps_.begin(), sps_.end());
        prepend_buffer_.insert(prepend_buffer_.end(), pps_.begin(), pps_.end());
        prepend_buffer_.insert(prepend_buffer_.end(), sei_.begin(), sei_.end());
        prepend_buffer_.insert(prepend_buffer_.end(), data, data + size);
        out_data = prepend_buffer_.data();
        out_size = prepend_buffer_.size();
    } else {
        out_data = data;
        out_size = size;
    }

    return true;
}

void RtmpCodecConfig::ConfigureStream(AVStream* stream) const {
    if (!stream || !stream->codecpar) {
        return;
    }

    AVCodecParameters* par = stream->codecpar;
    par->codec_id          = ToAvCodecId(codec_type_);
    par->codec_type        = AVMEDIA_TYPE_VIDEO;
    par->bit_rate          = EstimateBitrate(width_, height_);
    par->codec_tag         = 0;
    par->width             = width_;
    par->height            = height_;
    par->profile           = FF_PROFILE_H264_BASELINE;

    stream->time_base      = {1, 90000};
    stream->duration       = static_cast<int64_t>(90000.0f / fps_);
    stream->r_frame_rate   = {static_cast<int>(fps_ * 1000), 1000};
    stream->avg_frame_rate = stream->r_frame_rate;
    stream->id             = stream->index;

    // Write SPS/PPS/SEI into extradata — required for playback
    size_t total_size = sps_.size() + pps_.size() + sei_.size();
    if (total_size == 0) {
        LOG_WARN("{}", "ConfigureStream: no SPS/PPS extracted yet");
        return;
    }

    auto* mem = static_cast<uint8_t*>(av_mallocz(total_size + AV_INPUT_BUFFER_PADDING_SIZE));
    if (mem) {
        size_t pos = 0;
        memcpy(mem + pos, sps_.data(), sps_.size());
        pos += sps_.size();
        memcpy(mem + pos, pps_.data(), pps_.size());
        pos += pps_.size();
        memcpy(mem + pos, sei_.data(), sei_.size());

        av_freep(&par->extradata);
        par->extradata      = mem;
        par->extradata_size = static_cast<int>(total_size);
    }

    LOG_INFO("ConfigureStream: codec={} {}x{} bitrate={} fps={}", static_cast<int>(codec_type_), width_,
             height_, par->bit_rate, fps_);
}

}  // namespace cosmo
