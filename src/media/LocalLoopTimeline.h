#pragma once

#include <cstdint>
#include <string>

namespace cosmo::media {

// A private local-file playback clock. A caller must only set newLoop after a
// successful seek on the same open local file. It must itself verify that the
// packet is a parameterized IDR or another codec-safe random access point;
// AV_PKT_FLAG_KEY alone is not proof. Live reconnects must reset this object.
struct LocalLoopInput {
    int64_t pts{0};
    int64_t dts{0};
    int64_t duration{0};
    int timeBaseNum{0};
    int timeBaseDen{0};
    bool newLoop{false};
    bool safeRandomAccess{false};
    // Only if the caller has admitted a valid FPS and an explicit CFR route.
    bool allowCfrFallback{false};
};

class LocalLoopTimeline {
public:
    struct Prepared {
        int64_t pts{0};
        int64_t dts{0};
        int64_t duration{0};
        int timeBaseNum{0};
        int timeBaseDen{0};
        bool syntheticCfr{false};
        // Previous packet had no duration; a previous same-loop DTS delta was
        // used at this seam. This is an estimate for VFR, not source evidence.
        bool usedDurationFallback{false};

    private:
        friend class LocalLoopTimeline;
        uint64_t revision_{0};
        bool valid_{false};
        bool nextSynthetic_{false};
        int64_t nextOffset_{0};
        int64_t nextDts_{0};
        int64_t nextDuration_{0};
        int64_t nextDelta_{0};
        int64_t nextMaxPts_{0};
        int64_t nextPresentationFloor_{0};
        bool nextHasPresentationFloor_{false};
        int nextNum_{0};
        int nextDen_{0};
    };

    // Transactional: Prepare does not alter either this clock or output on
    // failure. Commit only after the caller has successfully accepted the
    // packet; stale/already-committed tokens are rejected.
    bool Prepare(const LocalLoopInput& input, Prepared& output, std::string& reason) const;
    bool Commit(const Prepared& prepared, std::string& reason);
    void Reset();

private:
    uint64_t revision_{0};
    bool initialized_{false};
    bool synthetic_{false};
    int64_t offset_{0};
    int64_t last_dts_{0};
    int64_t last_duration_{0};
    int64_t last_delta_{0};
    int64_t max_pts_{0};
    int64_t presentation_floor_{0};
    bool has_presentation_floor_{false};
    int num_{0};
    int den_{0};
};

}  // namespace cosmo::media
