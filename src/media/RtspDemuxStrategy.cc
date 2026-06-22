// RtspDemuxStrategy — Rtsp Demux Strategy implementation.

#include "media/RtspDemuxStrategy.h"

#include "util/Log.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "libavformat/avformat.h"
#ifdef __cplusplus
}
#endif

static constexpr const char* kTag = "[DEMUX] ";

namespace cosmo::media {

std::string GetAvErr(int errorNo);  // defined in VideoDemuxer.cc

RtspDemuxStrategy::RtspDemuxStrategy(int pullTimeoutSec, int delayMs)
    : pull_timeout_sec_(pullTimeoutSec), delay_ms_(delayMs) {}

util::ErrorEnum RtspDemuxStrategy::OpenInput(AVFormatContext*& fmt_ctx, const std::string& filename) {
    AVDictionary* options = nullptr;
    std::string stimeout  = "5000000";
    std::string max_delay = "200000";
    if (pull_timeout_sec_ >= 1 && pull_timeout_sec_ <= 300) {
        stimeout = std::to_string(pull_timeout_sec_ * 1000000);
    }
    if (delay_ms_ >= 10 && delay_ms_ <= 10000) {
        max_delay = std::to_string(delay_ms_ * 1000);
    }
    av_dict_set(&options, "max_delay", max_delay.c_str(), 0);
    av_dict_set(&options, "rtsp_transport", "tcp", 0);
    av_dict_set(&options, "stimeout", stimeout.c_str(), 0);
    av_dict_set(&options, "fflags", "nobuffer", 0);

    int ret = avformat_open_input(&fmt_ctx, filename.c_str(), nullptr, &options);
    av_dict_free(&options);
    if (ret != 0 || !fmt_ctx) {
        if (fmt_ctx) {
            avformat_close_input(&fmt_ctx);
            fmt_ctx = nullptr;
        }
        LOG_WARN("{}Open {} failed. ret:{} [{}]", kTag, filename, ret, GetAvErr(ret));
        if (std::string::npos != GetAvErr(ret).find("Unauthorized")) {
            return util::ErrorEnum::DemuxOpenStreamUnauthorized;
        }
        return util::ErrorEnum::DemuxOpenStreamFail;
    }
    LOG_INFO("{}Open {}. ", kTag, filename);
    return util::ErrorEnum::Success;
}

}  // namespace cosmo::media
