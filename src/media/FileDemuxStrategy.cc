// FileDemuxStrategy — File Demux Strategy implementation.

#include "media/FileDemuxStrategy.h"

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

util::ErrorEnum FileDemuxStrategy::OpenInput(AVFormatContext*& fmt_ctx, const std::string& filename) {
    int ret = avformat_open_input(&fmt_ctx, filename.c_str(), nullptr, nullptr);
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
