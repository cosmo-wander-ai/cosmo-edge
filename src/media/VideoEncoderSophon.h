#pragma once

#include <cstdlib>
#include <memory>
#include <queue>

#include "bm_vpuenc_interface.h"
#include "bmlib_runtime.h"
#include "media/VideoEncoder.h"

namespace cosmo {
namespace media {

    class VideoEncoderSophon : public VideoEncoder {
    public:
        explicit VideoEncoderSophon(void* mediaHandle);

        ~VideoEncoderSophon() override;

        bool Open() override;

        VideoPacketPtr SendYUVFrame(void*) override;

    private:
        void Clean();

        void SendEndFrame();

        bool CopyToDMA(void* block, BmVpuFramebuffer* fb);

        BmVpuFramebuffer* GetUnusedFrameBuffer();

        // Extracted from SendYUVFrame/SendEndFrame to reduce nesting
        void CollectFrameBuffer(const BmVpuEncodedFrame& frame);

    private:
        const size_t ENC_PKT_DATA_SIZE           = 1024 * 1024;
        const size_t SEND_FRAME_BUFFER_MAX_COUNT = 5;
        const size_t GET_STREAM_BUFFER_MAX_COUNT = 5;

        int soc_idx_;
        int core_idx_;

        bm_handle_t handle_;

        BmVpuEncoder* encoder_ = nullptr;
        BmVpuEncParams enc_params_;

        BmVpuEncOpenParams open_params_;

        BmVpuEncInitialInfo initial_info_;

        BmVpuCodecFormat codec_fmt_ = BM_VPU_CODEC_FORMAT_H265;

        size_t bs_buffer_size_        = 0;
        uint32_t bs_buffer_alignment_ = 0;

        std::unique_ptr<BmVpuEncDMABuffer> bs_dma_buffer_;

        int num_src_fb_ = 0;
        std::unique_ptr<BmVpuFramebuffer[]> src_fbs_;
        std::unique_ptr<BmVpuEncDMABuffer[]> src_dma_bufs_;

        std::queue<BmVpuFramebuffer*> frame_unused_queue_;

        BmVpuRawFrame input_frame_;
        BmVpuEncodedFrame output_frame_;

        bool first_pkt_received_ = false;

        bool closed_ = false;
    };

}  // namespace media

}  // namespace cosmo
