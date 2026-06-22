#pragma once

#include <chrono>

namespace cosmo::service::camera {

enum class AlgDemuxStatus {
    AlgDemuxInit,
    AlgDemuxInvalidUrl,
    AlgDemuxOpened,
    AlgDemuxOpenFailed,
    AlgDemuxOpenUnauthorized,
    AlgDemuxReading,
    AlgDemuxReadEnd,
    AlgDemuxReadFailed,
    AlgDemuxClosed
};

struct AlgDemuxStatusInfo {
    AlgDemuxStatus status;
    std::chrono::steady_clock::time_point timePoint;
};

}  // namespace cosmo::service::camera
