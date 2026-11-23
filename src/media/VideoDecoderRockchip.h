#pragma once

#include <memory>

#include "media/DecoderParameterSets.h"
#include "media/VideoDecoder.h"

namespace cosmo::media {

class VideoDecoderCpu;
struct RockchipDecoderState;

/// Shared Rockchip MPP decoder with native DMA-BUF export and RGA materialization.
///
/// Compressed H.264/H.265 packets are decoded by the VPU. A decoded MPP frame
/// is retained on the decoder thread until the channel sampling decision; only
/// selected outputs are materialized into the compact I420 VideoFrame expected
/// by host-only business consumers. RKNN preprocessing consumes the retained
/// DMA-BUF directly; RGA performs the unavoidable host-bound materialization.
class VideoDecoderRockchip final : public VideoDecoder {
public:
    explicit VideoDecoderRockchip(size_t name);
    ~VideoDecoderRockchip() override;

    bool Open() override;
    bool Close() override;
    bool IsOpened() override;
    void SetCodecExtradata(const std::vector<uint8_t>& extradata) override;
    bool ReuseForStreamRestart(VideoCodecType type, int width, int height) override;

    bool SendPacket(const uint8_t* pkt, size_t len, int64_t frame_idx) override;
    VideoFramePtr GetFrame() override;
    DecodedVideoFrame GetDecodedFrame() override;

    static VideoDecoderCapability ProbeCapability(VideoCodecType type);

private:
    bool OpenMpp();
    bool ConfigureFrameGroup(size_t buffer_size);
    DecodedVideoFrame ReceiveMppFrame(bool& made_progress);
    void CleanMpp();

    std::unique_ptr<RockchipDecoderState> state_;
    std::unique_ptr<VideoDecoderCpu> fallback_;
    std::vector<uint8_t> codec_extradata_;
    DecoderParameterSets parameter_sets_;
};

}  // namespace cosmo::media
