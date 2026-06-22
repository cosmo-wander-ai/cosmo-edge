// Video stream push base class

#pragma once

#include <map>
#include <stdexcept>
#include <string>

#include "util/Exception.h"
#include "util/MsgBaseTypes.h"
#include "util/dto/FilterTypes.h"
#include "util/dto/OverviewTypes.h"

namespace cosmo {

class VideoStreamPush {
public:
    VideoStreamPush(const std::string& url, int width, int height, float fps);
    virtual ~VideoStreamPush();

    VideoStreamPush(const VideoStreamPush&)            = delete;
    VideoStreamPush& operator=(const VideoStreamPush&) = delete;

    void PushFrame(const uint8_t* data, size_t size);
    void SetVideoAttr(int width, int height, float fps);
    [[nodiscard]] MsgOverviewDebugInfo GetProcInfo() const {
        return debug_info_;
    }

protected:
    virtual void DoPushFrame(const uint8_t* data, size_t size) = 0;

protected:
    std::string push_url_;
    int width_{1920};
    int height_{1080};
    float fps_{25};
    MsgOverviewDebugInfo debug_info_;
};

}  // namespace cosmo
