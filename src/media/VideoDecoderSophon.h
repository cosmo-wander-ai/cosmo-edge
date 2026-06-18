#pragma once

#include <mutex>

#ifdef __cplusplus
extern "C" {
#endif
#include "bm_vpudec_interface.h"
#ifdef __cplusplus
}
#endif

#include "bmcv_api_ext.h"
#include "media/VideoDecoder.h"

namespace cosmo {
namespace media {

    class VideoDecoderSophon : public VideoDecoder {
    public:
        VideoDecoderSophon(size_t name, void* mediaHandle);

        ~VideoDecoderSophon() override;

        bool Open() override;
        bool Close() override;

        bool IsOpened() override;

        bool SendPacket(const uint8_t* pkt, size_t len, int64_t frame_idx) override;

        VideoFramePtr GetFrame() override;

    private:
        bool AttachVideoFrame(bm_image* image, BMVidFrame* frame);

        bool AttachOutputFrame(bm_image* image, BMVidFrame* frame, VideoFramePtr frame_ptr);

    private:
        bm_handle_t bm_handle_;
        BMVidCodHandle code_handle_;

        BmVpuDecStreamFormat stream_format_;

        std::unique_ptr<BMVidFrame> frame_;

        size_t cache_size_ = 5;

        std::mutex close_mutex_;
        bool stop_   = true;
        bool opened_ = false;
    };

}  // namespace media

}  // namespace cosmo
