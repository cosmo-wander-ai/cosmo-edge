#pragma once

#include <cstddef>
#include <cstdint>

namespace cosmo {

enum class TrackObservation { kSkipped, kPositive, kNegative, kUnavailable };

// Positive evidence remains latched for the entire track. Skipped observations
// keep the track alive, but never contribute negative evidence.
class PositiveTrackEvidence {
public:
    void Observe(int64_t timestamp, TrackObservation observation, int64_t sample_interval_ms);
    void Invalidate();
    bool IsPositive() const;
    bool HasExpired(int64_t timestamp, int64_t timeout_ms) const;
    bool CanAlarm(size_t min_valid_count, int64_t min_duration_ms) const;

private:
    int64_t first_seen_{-1};
    int64_t last_seen_{-1};
    int64_t last_sample_{-1};
    size_t valid_count_{0};
    bool is_positive_{false};
    bool is_unavailable_{false};
};

}  // namespace cosmo
