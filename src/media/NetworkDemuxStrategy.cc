#include "media/NetworkDemuxStrategy.h"

#include "util/Log.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "libavformat/avformat.h"
#ifdef __cplusplus
}
#endif

namespace cosmo::media {

namespace {
    constexpr const char* kTag = "[DEMUX] ";
}

std::string GetAvErr(int errorNo);  // defined in VideoDemuxer.cc

NetworkDemuxStrategy::NetworkDemuxStrategy(int pullTimeoutSec) : pull_timeout_sec_(pullTimeoutSec) {}

util::ErrorEnum NetworkDemuxStrategy::OpenInput(AVFormatContext*& fmt_ctx, const std::string& filename) {
    AVDictionary* options = nullptr;
    std::string timeout   = "5000000";
    if (pull_timeout_sec_ >= 1 && pull_timeout_sec_ <= 300) {
        timeout = std::to_string(pull_timeout_sec_ * 1000000);
    }
    av_dict_set(&options, "rw_timeout", timeout.c_str(), 0);
    av_dict_set(&options, "fflags", "nobuffer", 0);

    const int ret = avformat_open_input(&fmt_ctx, filename.c_str(), nullptr, &options);
    av_dict_free(&options);
    if (ret != 0 || !fmt_ctx) {
        if (fmt_ctx) {
            avformat_close_input(&fmt_ctx);
            fmt_ctx = nullptr;
        }
        LOG_WARN("{}Open {} failed. ret:{} [{}]", kTag, filename, ret, GetAvErr(ret));
        return util::ErrorEnum::DemuxOpenStreamFail;
    }
    LOG_INFO("{}Open {}. ", kTag, filename);
    return util::ErrorEnum::Success;
}

}  // namespace cosmo::media
