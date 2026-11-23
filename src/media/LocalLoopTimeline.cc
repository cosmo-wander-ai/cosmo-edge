#include "media/LocalLoopTimeline.h"

#include <limits>

extern "C" {
#include "libavutil/mathematics.h"
}

namespace cosmo::media {
namespace {
    constexpr auto kMin = std::numeric_limits<int64_t>::min();
    constexpr auto kMax = std::numeric_limits<int64_t>::max();

    bool Add(int64_t a, int64_t b, int64_t& out) {
        if ((b > 0 && a > kMax - b) || (b < 0 && a < kMin - b))
            return false;
        out = a + b;
        return true;
    }

    bool Subtract(int64_t a, int64_t b, int64_t& out) {
        if ((b > 0 && a < kMin + b) || (b < 0 && a > kMax + b))
            return false;
        out = a - b;
        return true;
    }
}  // namespace

bool LocalLoopTimeline::Prepare(const LocalLoopInput& in, Prepared& output, std::string& reason) const {
    reason.clear();
    const auto fail = [&](const char* text) {
        reason = text;
        return false;
    };
    if (revision_ == std::numeric_limits<uint64_t>::max())
        return fail("timeline revision exhausted");
    if (!initialized_ && in.newLoop)
        return fail("cannot repeat before the first packet");
    if ((!initialized_ || in.newLoop) && !in.safeRandomAccess)
        return fail("first packet is not a verified random access point");
    if (in.duration < 0)
        return fail("negative packet duration");
    const bool no_timebase    = in.timeBaseNum == 0 && in.timeBaseDen == 0;
    const bool valid_timebase = in.timeBaseNum > 0 && in.timeBaseDen > 0;
    if (!no_timebase && !valid_timebase)
        return fail("invalid or partial time base");
    const bool complete_time = valid_timebase && in.pts != kMin && in.dts != kMin;

    Prepared next;
    next.revision_ = revision_;
    next.valid_    = true;
    next.duration  = in.duration;
    if (!initialized_) {
        if (!complete_time && !in.allowCfrFallback)
            return fail("source timestamps missing and CFR fallback not admitted");
        next.nextSynthetic_ = !complete_time;
    } else {
        next.nextSynthetic_ = synthetic_;
        if (synthetic_ && (!no_timebase || !in.allowCfrFallback))
            return fail("forced CFR timing contract changed during local playback");
        if (!synthetic_ && !complete_time)
            return fail("source timestamps missing after timed mode was selected");
        if (!synthetic_ && (num_ != in.timeBaseNum || den_ != in.timeBaseDen))
            return fail("source time base changed during local playback");
    }

    next.syntheticCfr = next.nextSynthetic_;
    if (next.syntheticCfr) {
        // The caller supplies its admitted CFR writer clock; keep timestamps
        // opaque and explicitly zero the source timebase on every packet.
        next.pts         = in.pts;
        next.dts         = in.dts;
        next.timeBaseNum = 0;
        next.timeBaseDen = 0;
        output           = next;
        return true;
    }
    if (in.pts < in.dts)
        return fail("negative composition offset is unsupported");
    int64_t composition{};
    if (!Subtract(in.pts, in.dts, composition))
        return fail("composition offset overflows");

    int64_t dts_offset            = initialized_ ? dts_offset_ : 0;
    int64_t pts_offset            = initialized_ ? pts_offset_ : 0;
    int64_t seam_presentation_end = 0;
    if (in.newLoop) {
        if (!duration_consistent_)
            return fail("source duration differs from decode interval; new generation required");
        const int64_t seam_duration = last_duration_ > 0 ? last_duration_ : last_delta_;
        if (seam_duration <= 0)
            return fail("cannot determine previous loop's final duration");
        int64_t next_dts{}, presentation_end = max_presentation_end_, presentation_offset{};
        if (!Add(last_dts_, seam_duration, next_dts) || !Subtract(next_dts, in.dts, dts_offset))
            return fail("loop timestamp offset overflows");
        if (has_unknown_duration_) {
            int64_t estimated_end{};
            if (last_delta_ <= 0)
                return fail("cannot determine previous loop's presentation duration");
            if (!Add(max_unknown_duration_pts_, last_delta_, estimated_end))
                return fail("loop presentation end overflows");
            if (estimated_end > presentation_end)
                presentation_end = estimated_end;
        }
        if (!Subtract(presentation_end, in.pts, presentation_offset))
            return fail("loop presentation offset overflows");
        seam_presentation_end = presentation_end;
        // Shifting PTS further than DTS each loop would grow CTS without
        // bound and eventually increase playback delay or overflow FLV/MP4.
        if (presentation_offset > dts_offset)
            return fail("loop composition drift requires a new generation");
        pts_offset                = dts_offset;
        next.usedDurationFallback = last_duration_ == 0 || has_unknown_duration_;
    }
    int64_t pts{}, dts{}, end{}, presentation_end{};
    if (!Add(in.pts, pts_offset, pts) || !Add(in.dts, dts_offset, dts) || !Add(dts, in.duration, end) ||
        !Add(pts, in.duration, presentation_end))
        return fail("adjusted packet timestamp overflows");
    if (pts == kMin || dts == kMin || end == kMin)
        return fail("adjusted timestamp collides with missing timestamp sentinel");
    int64_t adjusted_cts{};
    if (!Subtract(pts, dts, adjusted_cts) || adjusted_cts < 0)
        return fail("adjusted composition offset is invalid");
    const AVRational base{in.timeBaseNum, in.timeBaseDen};
    const int64_t flv_cts = av_rescale_q(adjusted_cts, base, AVRational{1, 1000});
    const int64_t mp4_cts = av_rescale_q(adjusted_cts, base, AVRational{1, 90000});
    // Leave one output tick for independent PTS/DTS rounding by the muxers.
    if (flv_cts < 0 || flv_cts >= 0x7fffff || mp4_cts < 0 ||
        mp4_cts >= static_cast<int64_t>(std::numeric_limits<uint32_t>::max()))
        return fail("adjusted composition offset exceeds FLV or MP4 limits");
    const bool has_floor = in.newLoop || has_presentation_floor_;
    const int64_t floor  = in.newLoop ? seam_presentation_end : presentation_floor_;
    if (has_floor && pts < floor)
        return fail("loop presentation timestamp overlaps the preceding loop");
    int64_t delta = 0;
    if (initialized_) {
        if (dts <= last_dts_)
            return fail("non-increasing decode timestamp");
        if (!in.newLoop && !Subtract(dts, last_dts_, delta))
            return fail("decode timestamp delta overflows");
    }
    next.pts                   = pts;
    next.dts                   = dts;
    next.timeBaseNum           = in.timeBaseNum;
    next.timeBaseDen           = in.timeBaseDen;
    next.nextDtsOffset_        = dts_offset;
    next.nextPtsOffset_        = pts_offset;
    next.nextDts_              = dts;
    next.nextDuration_         = in.duration;
    next.nextDelta_            = delta;
    const bool continuing_loop = initialized_ && !in.newLoop;
    next.nextDurationConsistent_ =
        !continuing_loop || (duration_consistent_ && (last_duration_ == 0 || last_duration_ == delta));
    next.nextMaxPresentationEnd_ = continuing_loop && max_presentation_end_ > presentation_end
                                       ? max_presentation_end_
                                       : presentation_end;
    next.nextHasUnknownDuration_ = (continuing_loop && has_unknown_duration_) || in.duration == 0;
    next.nextMaxUnknownDurationPts_ =
        in.duration == 0 ? (continuing_loop && has_unknown_duration_ && max_unknown_duration_pts_ > pts
                                ? max_unknown_duration_pts_
                                : pts)
                         : (continuing_loop ? max_unknown_duration_pts_ : 0);
    next.nextHasPresentationFloor_ = has_floor;
    next.nextPresentationFloor_    = floor;
    next.nextNum_                  = in.timeBaseNum;
    next.nextDen_                  = in.timeBaseDen;
    output                         = next;
    return true;
}

bool LocalLoopTimeline::Commit(const Prepared& prepared, std::string& reason) {
    reason.clear();
    if (!prepared.valid_ || prepared.revision_ != revision_) {
        reason = "stale or invalid prepared packet";
        return false;
    }
    if (revision_ == std::numeric_limits<uint64_t>::max()) {
        reason = "timeline revision exhausted";
        return false;
    }
    initialized_              = true;
    synthetic_                = prepared.nextSynthetic_;
    dts_offset_               = prepared.nextDtsOffset_;
    pts_offset_               = prepared.nextPtsOffset_;
    last_dts_                 = prepared.nextDts_;
    last_duration_            = prepared.nextDuration_;
    last_delta_               = prepared.nextDelta_;
    max_presentation_end_     = prepared.nextMaxPresentationEnd_;
    max_unknown_duration_pts_ = prepared.nextMaxUnknownDurationPts_;
    has_unknown_duration_     = prepared.nextHasUnknownDuration_;
    duration_consistent_      = prepared.nextDurationConsistent_;
    has_presentation_floor_   = prepared.nextHasPresentationFloor_;
    presentation_floor_       = prepared.nextPresentationFloor_;
    num_                      = prepared.nextNum_;
    den_                      = prepared.nextDen_;
    ++revision_;
    return true;
}

void LocalLoopTimeline::Reset() {
    initialized_ = false;
    synthetic_   = false;
    dts_offset_  = 0;
    pts_offset_  = 0;
    last_dts_ = last_duration_ = last_delta_ = 0;
    max_presentation_end_                    = 0;
    max_unknown_duration_pts_                = 0;
    has_unknown_duration_                    = false;
    duration_consistent_                     = true;
    presentation_floor_                      = 0;
    has_presentation_floor_                  = false;
    num_ = den_ = 0;
    // Invalidate tokens prepared before reset; saturation fails closed.
    if (revision_ != std::numeric_limits<uint64_t>::max())
        ++revision_;
}

}  // namespace cosmo::media
