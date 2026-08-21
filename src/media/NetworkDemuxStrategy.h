#pragma once

#include "media/IDemuxStrategy.h"

namespace cosmo::media {

/// Generic packetized live network input such as RTMP/FLV.
class NetworkDemuxStrategy final : public IDemuxStrategy {
public:
    explicit NetworkDemuxStrategy(int pullTimeoutSec = 5);

    util::ErrorEnum OpenInput(AVFormatContext*& fmt_ctx, const std::string& filename) override;
    bool SupportsRepeat() const override {
        return false;
    }
    bool IsLive() const override {
        return true;
    }
    bool NeedsBsf(VideoCodecType codec) const override {
        return codec == VideoCodecType::kH264 || codec == VideoCodecType::kH265;
    }
    bool NeedsFpsControl() const override {
        return false;
    }
    bool ShouldUpdateFpsFromPts() const override {
        return true;
    }

private:
    int pull_timeout_sec_;
};

}  // namespace cosmo::media
