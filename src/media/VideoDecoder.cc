// VideoDecoder — Video Decoder implementation.

#include "media/VideoDecoder.h"

#include "util/Log.h"

static constexpr const char* kTag = "[DECODER] ";

namespace cosmo {
namespace media {

    VideoDecoder::VideoDecoder(size_t name) : idx_name_("atomicDecoder_" + std::to_string(name)) {
        LOG_INFO("{} Construction", idx_name_);
    }

    VideoDecoder::~VideoDecoder() {}

    void VideoDecoder::SetCodecType(VideoCodecType type, int valWidth, int valHeight) {
        codec_type_ = type;
        width_      = static_cast<size_t>(valWidth);
        height_     = static_cast<size_t>(valHeight);
    }

    size_t VideoDecoder::GetWidth() const {
        return width_;
    }

    size_t VideoDecoder::GetHeight() const {
        return height_;
    }

    VideoFramePtr VideoDecoder::Decode(const uint8_t* pkt, size_t len, int64_t frame_idx, bool& result) {
        auto send_result = SendPacket(pkt, len, frame_idx);
        send_pkt_cnt_ += 1;
        if (frame_idx % (25 * 60) == 0) {
            LOG_INFO("{} Decode {} Frames. Total Get Output {} Frames.", idx_name_, send_pkt_cnt_,
                     recv_frame_cnt_);
        }

        if (!send_result) {
            result = false;
            LOG_WARN(
                "{}{} Frame Size:{} Decode Failed. frameIndex:{} data:{:02x} {:02x} {:02x} {:02x} {:02x} "
                "{:02x}",
                kTag, idx_name_, len, frame_idx, pkt[0], pkt[1], pkt[2], pkt[3], pkt[4], pkt[5]);
            return nullptr;
        }

        result = true;

        auto frame = GetFrame();
        if (frame) {
            recv_frame_cnt_ += 1;
        }

        return frame;
    }

}  // namespace media
}  // namespace cosmo