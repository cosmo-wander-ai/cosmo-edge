#pragma once

#include "media/IDemuxStrategy.h"

namespace cosmo::media {

/// RTSP / generic network stream strategy.
/// Annex-B output, no BSF needed, no FPS throttling, live source.
class RtspDemuxStrategy final : public IDemuxStrategy {
public:
    RtspDemuxStrategy(int pullTimeoutSec = 5, int delayMs = 200);

    util::ErrorEnum OpenInput(AVFormatContext*& fmt_ctx, const std::string& filename) override;
    bool SupportsRepeat() const override {
        return false;
    }
    bool IsLive() const override {
        return true;
    }
    bool NeedsBsf(VideoCodecType /*codec*/) const override {
        return false;
    }
    bool NeedsFpsControl() const override {
        return false;
    }
    bool ShouldUpdateFpsFromPts() const override {
        return true;
    }

private:
    int pull_timeout_sec_;
    int delay_ms_;
};

}  // namespace cosmo::media
