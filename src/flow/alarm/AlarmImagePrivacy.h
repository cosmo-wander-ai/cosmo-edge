#pragma once

#include <string>
#include <vector>

#include "media/VideoFrame.h"
#include "util/AlarmImagePrivacy.h"

namespace cosmo {

// Creates the sole source for every alarm image variant, before JPEG encoding or upload.
// A requested policy always fails closed when same-frame metadata or processing is unavailable.
VideoFramePtr PrepareAlarmImage(const VideoFramePtr& frame, const std::string& taskId,
                                const util::AlarmImagePrivacy& privacy,
                                const std::vector<std::string>& expectedDetectors = {});

}  // namespace cosmo
