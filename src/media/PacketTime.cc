#include "media/PacketTime.h"

#include <limits>

extern "C" {
#include "libavutil/mathematics.h"
}

namespace cosmo::media {
namespace {
    constexpr int64_t kNoTimestamp = std::numeric_limits<int64_t>::min();

    bool Subtract(int64_t value, int64_t origin, int64_t& result) {
        if ((origin > 0 && value < std::numeric_limits<int64_t>::min() + origin) ||
            (origin < 0 && value > std::numeric_limits<int64_t>::max() + origin)) {
            return false;
        }
        result = value - origin;
        return true;
    }
}  // namespace

bool RescalePacketTime(int64_t pts, int64_t dts, int64_t duration, int tb_num, int tb_den, int64_t origin_dts,
                       int target_timescale, PacketTime& out, std::string& reason) {
    reason.clear();
    if (pts == kNoTimestamp || dts == kNoTimestamp || origin_dts == kNoTimestamp) {
        reason = "packet timestamps are missing";
        return false;
    }
    if (tb_num <= 0 || tb_den <= 0 || target_timescale <= 0 || duration < 0) {
        reason = "packet time base, output timescale or duration is invalid";
        return false;
    }
    int64_t relative_pts{};
    int64_t relative_dts{};
    if (!Subtract(pts, origin_dts, relative_pts) || !Subtract(dts, origin_dts, relative_dts)) {
        reason = "packet timestamp origin subtraction overflows";
        return false;
    }
    if (relative_dts < 0 || relative_pts < relative_dts) {
        reason = "packet timestamps precede the decode origin or presentation precedes decoding";
        return false;
    }
    if (duration > std::numeric_limits<int64_t>::max() - relative_dts) {
        reason = "packet decode end timestamp overflows";
        return false;
    }

    // Positive int rationals keep av_rescale_q's internal rational products
    // within int64_t. FFmpeg performs the potentially larger value product
    // without signed overflow and returns INT64_MIN when the result overflows.
    const AVRational source{tb_num, tb_den};
    const AVRational target{1, target_timescale};
    PacketTime converted;
    converted.pts     = av_rescale_q(relative_pts, source, target);
    converted.dts     = av_rescale_q(relative_dts, source, target);
    const int64_t end = av_rescale_q(relative_dts + duration, source, target);
    if (converted.pts < 0 || converted.dts < 0 || end < 0) {
        reason = "packet timestamp rescaling overflows";
        return false;
    }
    if (converted.pts < converted.dts || end < converted.dts) {
        reason = "packet timestamp rescaling violates decode order";
        return false;
    }
    // Rescale both endpoints so adjacent fractional durations share the same
    // rounding boundary. A missing/zero source duration remains zero.
    converted.duration = end - converted.dts;
    out                = converted;
    return true;
}

}  // namespace cosmo::media
