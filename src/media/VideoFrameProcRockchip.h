#pragma once

#include <atomic>

#include "media/VideoFrameProcCpu.h"

namespace cosmo::media {

/// General-purpose Rockchip RGA frame processor.
///
/// Frames exposed through the generic VideoFrame API remain owned by
/// CosmoEdge's host memory pool. Each admitted RGA operation imports those
/// buffers for one synchronous operation. Detector and classifier fast paths
/// bypass this host-facing API and bind their MPP/RGA DMA-BUF inputs directly.
class VideoFrameProcRockchip final : public VideoFrameProcCpu {
public:
    explicit VideoFrameProcRockchip(IOsdTextRenderer& osd_service);
    ~VideoFrameProcRockchip() override = default;

    VideoFramePtr BGR2I420(VideoFramePtr frame) override;
    VideoFramePtr RGB2I420(VideoFramePtr frame) override;
    VideoFramePtr I4202BGR(VideoFramePtr frame) override;
    VideoFramePtr I4202RGB(VideoFramePtr frame) override;
    VideoFramePtr Resize(VideoFramePtr src, int dst_height, int dst_width) override;

private:
    VideoFramePtr ConvertWithRga(const VideoFramePtr& frame, PixelFormat dst_format, int src_rga_format,
                                 int dst_rga_format, int color_mode, const char* operation);
    VideoFramePtr ResizeWithRga(const VideoFramePtr& frame, int dst_height, int dst_width,
                                const char* operation);
    void LogFallbackOnce(const char* operation, int status);

    std::atomic_flag fallback_warning_logged_ = ATOMIC_FLAG_INIT;
    std::atomic<bool> rga_unavailable_{false};
};

}  // namespace cosmo::media
