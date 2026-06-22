// VideoUtil — Video Util implementation.

#include "media/VideoUtil.h"

namespace cosmo {
namespace media {

    HFrameType GetFrameType(VideoCodecType codec, const uint8_t* data) {
        switch (codec) {
            case media::VideoCodecType::kH264:
                return GetH264FrameType(data);
            case media::VideoCodecType::kH265:
                return GetH265FrameType(data);
            default:
                return media::HFrameType::UNKNOWN;
        }
    }

    HFrameType GetH264FrameType(const uint8_t* data) {
        switch (data[data[2] == 0x01 ? 3 : 4] & 0x1F) {
            case 0x05:
                return HFrameType::I;
            case 0x01:
                return HFrameType::P;
            case 0x07:
                return HFrameType::SPS;
            case 0x08:
                return HFrameType::PPS;
            case 0x06:
                return HFrameType::SEI;
            case 0x09:
                return HFrameType::AUD;
            default:
                return HFrameType::UNKNOWN;
        }
    }

    HFrameType GetH265FrameType(const uint8_t* data) {
        /*
        The NALU header size is 2 bytes. The 1st bit is 0, the 2nd-7th bits are the NALU type, which indicates
        the data content type (VPS, SPS, PPS, SEI, I-frame or P-frame). The 8th-15th bits are 1.

        VPS (Video Parameter Set) NALU header value is 0x4001 (Hex), extracting 2nd-7th bits (40 & 0x7E) >> 1
        = 32 (Decimal) SPS (Sequence Parameter Set) NALU header value is 0x4201 (Hex), extracting 2nd-7th bits
        (42 & 0x7E) >> 1 = 33 (Decimal) PPS (Picture Parameter Set) NALU header value is 0x4401 (Hex),
        extracting 2nd-7th bits (44 & 0x7E) >> 1 = 34 (Decimal) SEI (Supplemental Enhancement Information)
        NALU header value is 0x4e01 (Hex), extracting 2nd-7th bits (4e & 0x7E) >> 1 = 39 (Decimal) I-Frame
        NALU header value is 0x2601 (Hex), extracting 2nd-7th bits (26 & 0x7E) >> 1 = 19 (Decimal) P-Frame
        NALU header value is 0x0201 (Hex), extracting 2nd-7th bits (02 & 0x7E) >> 1 = 1 (Decimal)
        */
        switch ((data[data[2] == 0x01 ? 3 : 4] & 0x7E) >> 1) {
            case 0:
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
            case 7:
            case 8:
            case 9:
                return HFrameType::P;
            case 16:
            case 17:
            case 18:
            case 19:
            case 20:
            case 21:
                return HFrameType::I;
            case 32:
                return HFrameType::VPS;
            case 33:
                return HFrameType::SPS;
            case 34:
                return HFrameType::PPS;
            case 35:
            case 36:
            case 37:
            case 38:
            case 39:
            case 40:
                return HFrameType::SEI;
            default:
                return HFrameType::UNKNOWN;
        }
    }

    size_t SeparateHVideoFrame(const uint8_t* data, size_t size) {
        // Default current is 0x000001 or 0x00000001 and look for the next one as the end
        for (size_t i = 3; i + 3 < size; ++i) {
            if (data[i] == 0x00 && data[i + 1] == 0x00 &&
                (data[i + 2] == 0x01 || (data[i + 2] == 0x00 && data[i + 3] == 0x01))) {
                return i;
            }
        }
        return size;
    }

    const uint8_t* RemoveHFrameSeparator(const uint8_t* data, size_t& removedSize) {
        removedSize = 0;
        for (size_t i = 0; i < 8; ++i) {
            if (data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x01) {
                removedSize = i + 3;
                return data + i + 3;
            }
        }
        return nullptr;
    }

}  // namespace media
}  // namespace cosmo
