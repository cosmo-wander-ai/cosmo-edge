// UsbDemuxStrategy — Usb Demux Strategy implementation.

#include "media/UsbDemuxStrategy.h"

#include "util/Log.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "libavdevice/avdevice.h"
#include "libavformat/avformat.h"
#ifdef __cplusplus
}
#endif

static constexpr const char* kTag = "[DEMUX] ";

namespace cosmo::media {

std::string GetAvErr(int errorNo);  // defined in VideoDemuxer.cc

int UsbDemuxStrategy::ParseUsbTier(const std::string& usbPath) const {
    size_t queryPos = usbPath.find('?');
    if (queryPos == std::string::npos) {
        return 0;
    }
    std::string query = usbPath.substr(queryPos + 1);
    std::string key   = "tier=";
    size_t tierPos    = query.find(key);
    if (tierPos == std::string::npos) {
        return 0;
    }
    int tier = 0;
    try {
        tier = std::stoi(query.substr(tierPos + key.size()));
    } catch (const std::exception&) {
        return 0;
    }
    if (tier < 0 || tier > 2) {
        return 0;
    }
    return tier;
}

util::ErrorEnum UsbDemuxStrategy::OpenInput(AVFormatContext*& fmt_ctx, const std::string& filename) {
    avdevice_register_all();
    AVInputFormat* inputFmt = av_find_input_format("v4l2");
    if (!inputFmt) {
        LOG_WARN("{}Open {} v4l2 input format not found", kTag, filename);
        return util::ErrorEnum::DemuxOpenStreamFail;
    }

    std::string usbPath  = filename.substr(6);
    size_t queryPos      = usbPath.find('?');
    std::string devIdx   = queryPos == std::string::npos ? usbPath : usbPath.substr(0, queryPos);
    int tier             = ParseUsbTier(usbPath);
    std::string openPath = "/dev/video" + devIdx;

    const char* fmtNames[]        = {"mjpg", "mjpeg"};
    const char* framerateRational = "30/1";
    const char* kPresets[][3]     = {
            {"1920x1080", "1280x720", "640x480"},  // tier 0
            {"1280x720", "1920x1080", "640x480"},  // tier 1
            {"640x480", "1280x720", "1920x1080"},  // tier 2
    };
    const char** usbTries = kPresets[tier];

    auto safeClose = [&]() {
        if (fmt_ctx) {
            avformat_close_input(&fmt_ctx);
            fmt_ctx = nullptr;
        }
    };

    int retUsb = -1;

    // Phase 1: try each resolution + each format
    for (int i = 0; (retUsb != 0 || !fmt_ctx) && i < 3; ++i) {
        for (int f = 0; (retUsb != 0 || !fmt_ctx) && f < 2; ++f) {
            AVDictionary* opts = nullptr;
            av_dict_set(&opts, "input_format", fmtNames[f], 0);
            av_dict_set(&opts, "video_size", usbTries[i], 0);
            av_dict_set(&opts, "framerate", framerateRational, 0);
            LOG_INFO("{}USB camera open {} via V4L2 {} {} @ {} fps (tier:{})", kTag, openPath, fmtNames[f],
                     usbTries[i], framerateRational, tier);
            retUsb = avformat_open_input(&fmt_ctx, openPath.c_str(), inputFmt, &opts);
            av_dict_free(&opts);
            if (retUsb == 0 && fmt_ctx)
                break;
            safeClose();
            if (retUsb != 0)
                LOG_WARN("{}Open {} try {} {} failed. ret:{} [{}]", kTag, filename, i, fmtNames[f], retUsb,
                         GetAvErr(retUsb));
        }
    }

    // Phase 2: fallback with libv4l2 + device default size/fps
    for (int f = 0; (retUsb != 0 || !fmt_ctx) && f < 2; ++f) {
        AVDictionary* opts = nullptr;
        av_dict_set(&opts, "input_format", fmtNames[f], 0);
        av_dict_set(&opts, "use_libv4l2", "1", 0);
        LOG_INFO("{}USB camera open {} via V4L2 {} (device default size/fps)", kTag, openPath, fmtNames[f]);
        retUsb = avformat_open_input(&fmt_ctx, openPath.c_str(), inputFmt, &opts);
        av_dict_free(&opts);
        if (retUsb == 0 && fmt_ctx)
            break;
        safeClose();
        if (retUsb != 0)
            LOG_WARN("{}Open {} default {} failed. ret:{} [{}]", kTag, filename, fmtNames[f], retUsb,
                     GetAvErr(retUsb));
    }

    // Phase 3: bare open with no options
    if (retUsb != 0 || !fmt_ctx) {
        LOG_INFO("{}USB camera open {} via V4L2 (no options, device default)", kTag, openPath);
        retUsb = avformat_open_input(&fmt_ctx, openPath.c_str(), inputFmt, nullptr);
        if (retUsb != 0 || !fmt_ctx) {
            safeClose();
            if (retUsb != 0)
                LOG_WARN("{}Open {} no-options failed. ret:{} [{}]", kTag, filename, retUsb,
                         GetAvErr(retUsb));
        }
    }

    if (retUsb != 0 || !fmt_ctx) {
        LOG_WARN("{}Open {} failed after all USB tries. ret:{} [{}]", kTag, filename, retUsb,
                 retUsb ? GetAvErr(retUsb) : "");
        if (retUsb != 0 && GetAvErr(retUsb).find("Unauthorized") != std::string::npos)
            return util::ErrorEnum::DemuxOpenStreamUnauthorized;
        return util::ErrorEnum::DemuxOpenStreamFail;
    }

    LOG_INFO("{}Open {} ok.", kTag, filename);
    return util::ErrorEnum::Success;
}

}  // namespace cosmo::media
