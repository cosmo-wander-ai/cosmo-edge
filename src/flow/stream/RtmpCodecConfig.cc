// RtmpCodecConfig — Rtmp Codec Config implementation.

#include "flow/stream/RtmpCodecConfig.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

#include "util/Log.h"

namespace cosmo {

namespace {
    // Bitrate tiers by pixel count
    constexpr int kPixels1080p = 1920 * 1080;
    constexpr int kPixels720p  = 1280 * 720;

    constexpr int64_t kBitrate1080p = 4000000;  // 4 Mbps
    constexpr int64_t kBitrate720p  = 2000000;  // 2 Mbps
    constexpr int64_t kBitrate480p  = 1000000;  // 1 Mbps

    constexpr float kDefaultFps             = 25.0F;
    constexpr float kMaxFps                 = 240.0F;
    constexpr size_t kMaxEncodedPacketBytes = 64ULL * 1024 * 1024;
    constexpr size_t kMaxParameterSetBytes  = 1024 * 1024;
    constexpr size_t kMaxExtradataBytes     = 4ULL * 1024 * 1024;

    float NormalizeFps(float fps) {
        return std::isfinite(fps) && fps > 0.0F && fps <= kMaxFps ? fps : kDefaultFps;
    }

    bool AppendParameterSet(std::vector<uint8_t>& output, const std::vector<uint8_t>& input, size_t offset,
                            size_t length) {
        static constexpr uint8_t kAnnexB[]{0x00, 0x00, 0x00, 0x01};
        if (length == 0 || length > kMaxParameterSetBytes || offset > input.size() ||
            length > input.size() - offset || output.size() > kMaxExtradataBytes - 4 ||
            length > kMaxExtradataBytes - output.size() - 4) {
            return false;
        }
        output.insert(output.end(), std::begin(kAnnexB), std::end(kAnnexB));
        output.insert(output.end(), input.begin() + static_cast<std::ptrdiff_t>(offset),
                      input.begin() + static_cast<std::ptrdiff_t>(offset + length));
        return true;
    }

    bool FindAnnexBStartCode(const uint8_t* data, size_t size, size_t from, size_t& offset, size_t& length) {
        for (size_t index = from; index + 3 <= size; ++index) {
            if (index + 4 <= size && data[index] == 0x00 && data[index + 1] == 0x00 &&
                data[index + 2] == 0x00 && data[index + 3] == 0x01) {
                offset = index;
                length = 4;
                return true;
            }
            if (data[index] == 0x00 && data[index + 1] == 0x00 && data[index + 2] == 0x01) {
                offset = index;
                length = 3;
                return true;
            }
        }
        return false;
    }
}  // namespace

RtmpCodecConfig::RtmpCodecConfig(media::VideoCodecType codec_type, int width, int height, float fps)
    : codec_type_(codec_type),
      width_(std::max(width, 1)),
      height_(std::max(height, 1)),
      fps_(NormalizeFps(fps)) {}

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
    if (width <= 0 || height <= 0) {
        return kBitrate480p;
    }
    const auto pixels = static_cast<int64_t>(width) * static_cast<int64_t>(height);
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

    if (!data || size == 0 || size > kMaxEncodedPacketBytes) {
        return false;
    }

    do {
        offset += sz;
        sz = media::SeparateHVideoFrame(data + offset, size - offset);

        auto assign = [](auto& v, const uint8_t* d, size_t off, size_t len) {
            if (v.empty() && len <= kMaxParameterSetBytes) {
                v.assign(d + off, d + off + len);
            }
        };

        auto type = media::GetFrameType(codec_type_, data + offset, sz);
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

    lead_type = media::GetFrameType(codec_type_, data, size);
    if (!has_video_slice) {
        return false;
    }

    main_type = media::GetFrameType(codec_type_, data + last_frame_offset, size - last_frame_offset);

    // If I-frame lacks SPS prefix, prepend cached VPS/SPS/PPS/SEI
    // SPS/PPS from in-band stream have 00 00 00 01 prefix (SeparateHVideoFrame
    // includes it); those from avcC extradata are raw NAL units.
    // Annex-B-mix would corrupt the bytestream — detect and add prefix as needed.
    const uint8_t* effective_data = data;
    size_t effective_size         = size;
    if (main_type == media::HFrameType::I && lead_type != media::HFrameType::SPS &&
        lead_type != media::HFrameType::VPS) {
        static const uint8_t kStartCode[]{0x00, 0x00, 0x00, 0x01};
        auto needsPrefix = [](const std::vector<uint8_t>& nal) {
            return !nal.empty() &&
                   (nal.size() < 4 || nal[0] != 0x00 || nal[1] != 0x00 || nal[2] != 0x00 || nal[3] != 0x01);
        };
        auto appendNal = [&](const std::vector<uint8_t>& nal) {
            if (nal.empty())
                return;
            if (needsPrefix(nal)) {
                prepend_buffer_.insert(prepend_buffer_.end(), kStartCode, kStartCode + 4);
            }
            prepend_buffer_.insert(prepend_buffer_.end(), nal.begin(), nal.end());
        };

        prepend_buffer_.clear();
        prepend_buffer_.reserve(4 * 4 + vps_.size() + sps_.size() + pps_.size() + sei_.size() + size);
        appendNal(vps_);
        appendNal(sps_);
        appendNal(pps_);
        appendNal(sei_);
        prepend_buffer_.insert(prepend_buffer_.end(), data, data + size);
        effective_data = prepend_buffer_.data();
        effective_size = prepend_buffer_.size();
    }

    // FFmpeg 4.4's FLV muxer can emit zero-length AVCC NAL units when an Annex-B packet
    // contains legal trailing_zero_8bits (for example 00 00 00 00 00 01 between PPS and
    // SEI). Rebuild a canonical Annex-B packet so the legacy muxer receives exactly one
    // start code per non-empty NAL. This preserves the existing FFmpeg ABI while making
    // periodic keyframes decodable by standard H.264 clients.
    if (!NormalizeAnnexB(effective_data, effective_size, out_data, out_size)) {
        out_data = effective_data;
        out_size = effective_size;
    }

    return true;
}

bool RtmpCodecConfig::NormalizeAnnexB(const uint8_t* data, size_t size, const uint8_t*& out_data,
                                      size_t& out_size) {
    size_t start_offset = 0;
    size_t start_length = 0;
    if (!data || !FindAnnexBStartCode(data, size, 0, start_offset, start_length)) {
        return false;
    }

    static constexpr uint8_t kStartCode[]{0x00, 0x00, 0x00, 0x01};
    normalized_buffer_.clear();
    normalized_buffer_.reserve(size + 16);
    bool needs_normalization = start_offset != 0 || start_length != sizeof(kStartCode);

    size_t nal_start = start_offset + start_length;
    while (nal_start < size) {
        size_t next_offset             = 0;
        size_t next_length             = 0;
        const bool has_next            = FindAnnexBStartCode(data, size, nal_start, next_offset, next_length);
        size_t nal_end                 = has_next ? next_offset : size;
        const size_t untrimmed_nal_end = nal_end;
        while (nal_end > nal_start && data[nal_end - 1] == 0x00) {
            --nal_end;
        }
        if (nal_end > nal_start) {
            const size_t nal_size = nal_end - nal_start;
            if (normalized_buffer_.size() > kMaxEncodedPacketBytes - sizeof(kStartCode) ||
                nal_size > kMaxEncodedPacketBytes - normalized_buffer_.size() - sizeof(kStartCode)) {
                normalized_buffer_.clear();
                return false;
            }
            normalized_buffer_.insert(normalized_buffer_.end(), kStartCode, kStartCode + 4);
            normalized_buffer_.insert(normalized_buffer_.end(), data + nal_start, data + nal_end);
        }
        needs_normalization = needs_normalization || nal_end != untrimmed_nal_end ||
                              (has_next && next_length != sizeof(kStartCode));
        if (!has_next) {
            break;
        }
        nal_start = next_offset + next_length;
    }

    if (normalized_buffer_.empty() || !needs_normalization) {
        normalized_buffer_.clear();
        return false;
    }
    out_data = normalized_buffer_.data();
    out_size = normalized_buffer_.size();
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
    par->profile =
        codec_type_ == media::VideoCodecType::kH265 ? FF_PROFILE_HEVC_MAIN : FF_PROFILE_H264_BASELINE;

    stream->time_base      = {1, 90000};
    stream->duration       = static_cast<int64_t>(90000.0f / fps_);
    stream->r_frame_rate   = {static_cast<int>(fps_ * 1000), 1000};
    stream->avg_frame_rate = stream->r_frame_rate;
    stream->id             = stream->index;

    // Write SPS/PPS/SEI into extradata — required for playback
    const size_t total_size = vps_.size() + sps_.size() + pps_.size() + sei_.size();
    if (total_size == 0 || total_size > kMaxExtradataBytes ||
        total_size > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        total_size > std::numeric_limits<size_t>::max() - AV_INPUT_BUFFER_PADDING_SIZE) {
        LOG_WARN("{}", "ConfigureStream: no SPS/PPS extracted yet");
        return;
    }

    auto* mem = static_cast<uint8_t*>(av_mallocz(total_size + AV_INPUT_BUFFER_PADDING_SIZE));
    if (mem) {
        size_t pos          = 0;
        auto copy_parameter = [&](const std::vector<uint8_t>& parameter) {
            if (!parameter.empty()) {
                memcpy(mem + pos, parameter.data(), parameter.size());
                pos += parameter.size();
            }
        };
        copy_parameter(vps_);
        copy_parameter(sps_);
        copy_parameter(pps_);
        copy_parameter(sei_);

        av_freep(&par->extradata);
        par->extradata      = mem;
        par->extradata_size = static_cast<int>(total_size);
    }

    LOG_INFO("ConfigureStream: codec={} {}x{} bitrate={} fps={}", static_cast<int>(codec_type_), width_,
             height_, par->bit_rate, fps_);
}

void RtmpCodecConfig::SetParameters(const std::vector<uint8_t>& extradata) {
    if (extradata.empty() || extradata.size() > kMaxExtradataBytes) {
        LOG_WARN("SetParameters: extradata empty or too small ({} bytes)", extradata.size());
        return;
    }
    bool parsed = false;
    switch (codec_type_) {
        case media::VideoCodecType::kH264:
            parsed = ParseAvcC(extradata);
            break;
        case media::VideoCodecType::kH265:
            parsed = ParseHevcC(extradata);
            break;
        default:
            break;
    }
    if (!parsed) {
        LOG_WARN("SetParameters: reject malformed codec extradata ({} bytes)", extradata.size());
    }
}

bool RtmpCodecConfig::ParseAvcC(const std::vector<uint8_t>& data) {
    // ISO 14496-15 AVCDecoderConfigurationRecord
    // byte 0: configurationVersion (=1)
    // byte 1: AVCProfileIndication
    // byte 2: profile_compatibility
    // byte 3: AVCLevelIndication
    // byte 4: 0xFC | (lengthSizeMinusOne & 0x03)
    // byte 5: 0xE0 | (numOfSequenceParameterSets & 0x1F)
    //   for each SPS: uint16(spsLength) + spsNALUnit
    // byte: numOfPictureParameterSets
    //   for each PPS: uint16(ppsLength) + ppsNALUnit

    // NAL units from avcC extradata are raw (no Annex B start code).
    // Add 00 00 00 01 so ConfigureStream writes valid Annex B extradata
    // (FFmpeg FLV muxer auto-detects Annex B → AVCC), and so ParseAndPrepare
    // can prepend them to I-frame bytestream data correctly.
    if (data.size() < 7 || data[0] != 1)
        return false;

    size_t pos      = 5;                 // skip configurationVersion + 3 profile bytes + lengthSize
    uint8_t num_sps = data[pos] & 0x1F;  // lower 5 bits
    pos++;
    if (num_sps == 0) {
        return false;
    }
    std::vector<uint8_t> parsed_sps;
    std::vector<uint8_t> parsed_pps;

    for (uint8_t i = 0; i < num_sps; ++i) {
        if (pos >= data.size() || data.size() - pos < 2)
            return false;
        uint16_t sps_len = (static_cast<uint16_t>(data[pos]) << 8) | data[pos + 1];
        pos += 2;
        if (!AppendParameterSet(parsed_sps, data, pos, sps_len))
            return false;
        pos += sps_len;
    }

    if (pos >= data.size())
        return false;
    uint8_t num_pps = data[pos];
    pos++;
    if (num_pps == 0) {
        return false;
    }

    for (uint8_t i = 0; i < num_pps; ++i) {
        if (pos >= data.size() || data.size() - pos < 2)
            return false;
        uint16_t pps_len = (static_cast<uint16_t>(data[pos]) << 8) | data[pos + 1];
        pos += 2;
        if (!AppendParameterSet(parsed_pps, data, pos, pps_len))
            return false;
        pos += pps_len;
    }

    sps_ = std::move(parsed_sps);
    pps_ = std::move(parsed_pps);
    LOG_INFO("ParseAvcC: extracted SPS({}B) PPS({}B)", sps_.size(), pps_.size());
    return true;
}

bool RtmpCodecConfig::ParseHevcC(const std::vector<uint8_t>& data) {
    // ISO/IEC 14496-15 HEVCDecoderConfigurationRecord: byte 22 is
    // numOfArrays, followed by type/count/length-delimited NAL units.
    if (data.size() < 23 || data[0] != 1) {
        return false;
    }
    std::vector<uint8_t> parsed_vps;
    std::vector<uint8_t> parsed_sps;
    std::vector<uint8_t> parsed_pps;
    size_t pos             = 23;
    const auto array_count = data[22];
    for (uint8_t array_index = 0; array_index < array_count; ++array_index) {
        if (pos >= data.size() || data.size() - pos < 3) {
            return false;
        }
        const auto nalu_type  = static_cast<uint8_t>(data[pos] & 0x3F);
        const auto nalu_count = static_cast<uint16_t>(data[pos + 1] << 8) | data[pos + 2];
        pos += 3;
        for (uint16_t nalu_index = 0; nalu_index < nalu_count; ++nalu_index) {
            if (pos >= data.size() || data.size() - pos < 2) {
                return false;
            }
            const auto nalu_size = static_cast<uint16_t>(data[pos] << 8) | data[pos + 1];
            pos += 2;
            auto* destination = nalu_type == 32   ? &parsed_vps
                                : nalu_type == 33 ? &parsed_sps
                                : nalu_type == 34 ? &parsed_pps
                                                  : nullptr;
            if (destination != nullptr && !AppendParameterSet(*destination, data, pos, nalu_size)) {
                return false;
            }
            if (static_cast<size_t>(nalu_size) > data.size() - pos) {
                return false;
            }
            pos += nalu_size;
        }
    }
    if (parsed_vps.empty() || parsed_sps.empty() || parsed_pps.empty()) {
        return false;
    }
    vps_ = std::move(parsed_vps);
    sps_ = std::move(parsed_sps);
    pps_ = std::move(parsed_pps);
    LOG_INFO("ParseHevcC: extracted VPS({}B) SPS({}B) PPS({}B)", vps_.size(), sps_.size(), pps_.size());
    return true;
}

}  // namespace cosmo
