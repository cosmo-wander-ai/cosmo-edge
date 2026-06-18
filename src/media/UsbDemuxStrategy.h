#pragma once

#include "media/IDemuxStrategy.h"

namespace cosmo::media {

/// USB/V4L2 camera strategy.
/// MJPEG input, no BSF needed, no FPS throttling, live source.
class UsbDemuxStrategy final : public IDemuxStrategy {
public:
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
    int ParseUsbTier(const std::string& usbPath) const;
};

}  // namespace cosmo::media
