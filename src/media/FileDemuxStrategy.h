#pragma once

#include "media/IDemuxStrategy.h"

namespace cosmo::media {

/// Local file strategy (MP4, AVI, etc).
/// Supports looping, BSF for H264/H265, FPS throttling, not a live source.
class FileDemuxStrategy final : public IDemuxStrategy {
public:
    util::ErrorEnum OpenInput(AVFormatContext*& fmt_ctx, const std::string& filename) override;
    bool SupportsRepeat() const override {
        return true;
    }
    bool IsLive() const override {
        return false;
    }
    bool NeedsBsf(VideoCodecType codec) const override {
        return codec == VideoCodecType::kH264 || codec == VideoCodecType::kH265;
    }
    bool NeedsFpsControl() const override {
        return true;
    }
    bool ShouldUpdateFpsFromPts() const override {
        return false;
    }
};

}  // namespace cosmo::media
