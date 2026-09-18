#include "flow/sensitivity/PositiveTrackEvidence.h"

#include <limits>

namespace cosmo {

void PositiveTrackEvidence::Observe(int64_t timestamp, TrackObservation observation,
                                    int64_t sample_interval_ms) {
    if (timestamp < 0 || timestamp < last_seen_) {
        return;
    }
    if (first_seen_ < 0) {
        first_seen_ = timestamp;
    }
    last_seen_ = timestamp;
    if (observation == TrackObservation::kPositive) {
        is_positive_ = true;
    } else if (observation == TrackObservation::kUnavailable) {
        Invalidate();
    } else if (observation == TrackObservation::kNegative && !is_positive_ && !is_unavailable_ &&
               (last_sample_ < 0 ||
                (timestamp > last_sample_ && timestamp - last_sample_ >= sample_interval_ms))) {
        if (valid_count_ < std::numeric_limits<size_t>::max()) {
            ++valid_count_;
        }
        last_sample_ = timestamp;
    }
}

void PositiveTrackEvidence::Invalidate() {
    is_unavailable_ = true;
}

bool PositiveTrackEvidence::IsPositive() const {
    return is_positive_;
}

bool PositiveTrackEvidence::HasExpired(int64_t timestamp, int64_t timeout_ms) const {
    return last_seen_ >= 0 && timestamp >= last_seen_ && timestamp - last_seen_ >= timeout_ms;
}

bool PositiveTrackEvidence::CanAlarm(size_t min_valid_count, int64_t min_duration_ms) const {
    return first_seen_ >= 0 && !is_positive_ && !is_unavailable_ && min_valid_count > 0 &&
           valid_count_ >= min_valid_count && last_seen_ - first_seen_ >= min_duration_ms;
}

}  // namespace cosmo
