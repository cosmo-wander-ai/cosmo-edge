// VideoStreamPush — Video stream push base class

#include "flow/stream/VideoStreamPush.h"

#include "util/Log.h"

namespace cosmo {

VideoStreamPush::VideoStreamPush(const std::string& url, int width, int height, float fps)
    : push_url_(std::move(url)) {
    if ((width > 1920) || (height > 1088)) {
        // In case of 400w input, decode to 1080P. Reference: CopyYUV420SPLimitArea
        LOG_WARN("Push Src is {}x{} But Max Support is 1080P Modify To 1080P.", width, height);
        width  = 1920;
        height = 1080;
    }
    LOG_INFO("rtmp push url: {}, width: {}, height: {}, fps: {} ", push_url_, width, height, fps);

    if ((width < 64) || (height < 64) || (fps < 0.1f))
        return;

    width_  = width;
    height_ = height;
    fps_    = fps;
}

VideoStreamPush::~VideoStreamPush() {
    LOG_INFO("{}", "");
}

void VideoStreamPush::PushFrame(const uint8_t* data, size_t size) {
    DoPushFrame(data, size);
}

void VideoStreamPush::SetVideoAttr(int width, int height, float fps) {
    if ((width < 64) || (height < 64) || (fps < 0.1f))
        return;
    width_  = width;
    height_ = height;
    fps_    = fps;
}

}  // namespace cosmo
