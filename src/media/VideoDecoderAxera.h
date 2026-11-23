#pragma once

#include <memory>

#include "media/VideoDecoder.h"
#include "media/VideoPacket.h"

namespace cosmo::media {

struct AxeraDecoderState;

/// The existing GetFrame path remains available for current consumers. The
/// explicit native API never materializes host pixels and is experimental.
class VideoDecoderAxera final : public VideoDecoder {
public:
    explicit VideoDecoderAxera(size_t name);
    ~VideoDecoderAxera() override;

    bool Open() override;
    bool Close() override;
    bool IsOpened() override;
    bool SendPacket(const uint8_t* packet, size_t size, int64_t frame_index) override;
    // Parses Annex-B SPS data and resolves the color matrix from bitstream,
    // media metadata, or the documented AX application default. The chosen
    // source is recorded and associated with the decoder's synthetic token.
    bool SendNativePacket(const VideoPacket& packet);
    DecodedVideoFrame DecodeNativePacket(const VideoPacket& packet, bool& result);
    VideoFramePtr GetFrame() override;
    DecodedVideoFrame GetDecodedFrame() override;
    // Explicit and sticky for this decoder instance. In native-only mode,
    // GetFrame and deferred materialization must never copy pixels to CPU.
    void RequireNativeFrames();
    [[nodiscard]] bool NativeOnly() const;
    // A failed Close can mean consumers still hold frames; no new input is accepted.
    [[nodiscard]] bool NativeClosing() const;
    // Only the decoder owner may release a returned lease and admit input.
    bool PrepareNativeInput();
    bool PrepareNativeSelection();
    NativeVideoBufferPtr GetNativeFrame();
    static VideoDecoderCapability Probe(VideoCodecType type);

private:
    bool SendPacketImpl(const uint8_t* packet, size_t size, int64_t frame_index,
                        const VideoPacket* color_packet);
    std::shared_ptr<AxeraDecoderState> state_;
};

}  // namespace cosmo::media
