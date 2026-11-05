#include "media/LocalLoopTimeline.h"

#include <limits>

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

    int64_t offset = initialized_ ? offset_ : 0;
    if (in.newLoop) {
        const int64_t seam_duration = last_duration_ > 0 ? last_duration_ : last_delta_;
        if (seam_duration <= 0)
            return fail("cannot determine previous loop's final duration");
        int64_t next_dts{};
        if (!Add(last_dts_, seam_duration, next_dts) || !Subtract(next_dts, in.dts, offset))
            return fail("loop timestamp offset overflows");
        next.usedDurationFallback = last_duration_ == 0;
    }
    int64_t pts{}, dts{}, end{};
    if (!Add(in.pts, offset, pts) || !Add(in.dts, offset, dts) || !Add(dts, in.duration, end))
        return fail("adjusted packet timestamp overflows");
    if (pts == kMin || dts == kMin || end == kMin)
        return fail("adjusted timestamp collides with missing timestamp sentinel");
    const bool has_floor = in.newLoop || has_presentation_floor_;
    const int64_t floor  = in.newLoop ? max_pts_ : presentation_floor_;
    if (has_floor && pts <= floor)
        return fail("loop presentation timestamp overlaps the preceding loop");
    int64_t delta = 0;
    if (initialized_) {
        if (dts <= last_dts_)
            return fail("non-increasing decode timestamp");
        if (!in.newLoop && !Subtract(dts, last_dts_, delta))
            return fail("decode timestamp delta overflows");
    }
    next.pts                       = pts;
    next.dts                       = dts;
    next.timeBaseNum               = in.timeBaseNum;
    next.timeBaseDen               = in.timeBaseDen;
    next.nextOffset_               = offset;
    next.nextDts_                  = dts;
    next.nextDuration_             = in.duration;
    next.nextDelta_                = delta;
    next.nextMaxPts_               = initialized_ && max_pts_ > pts ? max_pts_ : pts;
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
    initialized_            = true;
    synthetic_              = prepared.nextSynthetic_;
    offset_                 = prepared.nextOffset_;
    last_dts_               = prepared.nextDts_;
    last_duration_          = prepared.nextDuration_;
    last_delta_             = prepared.nextDelta_;
    max_pts_                = prepared.nextMaxPts_;
    has_presentation_floor_ = prepared.nextHasPresentationFloor_;
    presentation_floor_     = prepared.nextPresentationFloor_;
    num_                    = prepared.nextNum_;
    den_                    = prepared.nextDen_;
    ++revision_;
    return true;
}

void LocalLoopTimeline::Reset() {
    initialized_ = false;
    synthetic_   = false;
    offset_      = 0;
    last_dts_ = last_duration_ = last_delta_ = 0;
    max_pts_                                 = 0;
    presentation_floor_                      = 0;
    has_presentation_floor_                  = false;
    num_ = den_ = 0;
    // Invalidate tokens prepared before reset; saturation fails closed.
    if (revision_ != std::numeric_limits<uint64_t>::max())
        ++revision_;
}

}  // namespace cosmo::media
