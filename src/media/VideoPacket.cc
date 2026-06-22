// VideoPacket — Legacy global alias — kept for backward compatibility.

#include "media/VideoPacket.h"

namespace cosmo::media {

uint8_t* VideoPacket::GetData() {
    return data.data();
}

size_t VideoPacket::GetSize() const {
    return data.size();
}

int64_t VideoPacket::GetSequence() const {
    return index;
}

int64_t VideoPacket::GetTimestamp() const {
    return timestamp;
}

int64_t VideoPacket::GetTimestampEpoch() const {
    return timestamp_epoch;
}

VideoCodecType VideoPacket::GetType() const {
    return codec_type;
}

size_t VideoPacket::GetWidth() const {
    return width;
}

size_t VideoPacket::GetHeight() const {
    return height;
}

float VideoPacket::GetFPS() const {
    return fps;
}

bool VideoPacket::IsIFrame() const {
    if (data.size() < 5)
        return is_i_frame;

    bool found_i_frame = false;
    bool found_p_frame = false;

    // Search the first few bytes (up to 32) for the start code and NAL unit type.
    // H.264/H.265 stream packets can begin with SEI, AUD, or directly with SPS/PPS/IDR.
    size_t search_len = data.size() < 32 ? data.size() - 4 : 28;
    for (size_t i = 0; i <= search_len; ++i) {
        uint8_t nal_type   = 0xFF;
        bool is_start_code = false;

        if (data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x01) {
            nal_type      = data[i + 3];
            is_start_code = true;
        } else if (data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x00 && data[i + 3] == 0x01) {
            nal_type      = data[i + 4];
            is_start_code = true;
        }

        if (is_start_code) {
            if (codec_type == VideoCodecType::kH264) {
                uint8_t type = nal_type & 0x1F;
                if (type == 5 || type == 7 || type == 8) {  // IDR, SPS, PPS
                    found_i_frame = true;
                } else if (type == 1) {  // Non-IDR (P/B frame)
                    found_p_frame = true;
                }
            } else if (codec_type == VideoCodecType::kH265) {
                uint8_t type = (nal_type & 0x7E) >> 1;
                if (type >= 16 && type <= 23) {  // IRAP pictures (CRA, IDR, BLA)
                    found_i_frame = true;
                } else if (type >= 32 && type <= 34) {  // VPS, SPS, PPS
                    found_i_frame = true;
                } else if (type <= 1) {  // TRAIL_N, TRAIL_R (P/B frames)
                    found_p_frame = true;
                }
            }
        }
        if (found_i_frame || found_p_frame) {
            break;
        }
    }

    if (found_i_frame)
        return true;
    if (found_p_frame)
        return false;

    return is_i_frame;
}

}  // namespace cosmo::media