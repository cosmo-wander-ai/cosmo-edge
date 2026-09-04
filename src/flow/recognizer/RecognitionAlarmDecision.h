#pragma once

#include <cstddef>
#include <unordered_map>
#include <unordered_set>

namespace cosmo {

enum class MatchFlagType {
    NotMatch = 0,  // Alarm on a confirmed non-match
    Match,         // Alarm on a match
    All,           // Alarm on every valid comparison
    Max,
};

constexpr bool IsValidMatchFlagType(int value) noexcept {
    return value >= static_cast<int>(MatchFlagType::NotMatch) && value < static_cast<int>(MatchFlagType::Max);
}

struct RecognitionAlarmDecisionConfig {
    MatchFlagType match_flag{MatchFlagType::Match};
    size_t stranger_confirm_count{3};
};

// Converts per-frame comparison results into per-track alarm decisions. The
// implementation deliberately keeps the temporal confirmation state behind a
// small interface so recognition and alarm reporting do not duplicate policy.
class RecognitionAlarmDecision {
public:
    void SetConfig(RecognitionAlarmDecisionConfig config);
    [[nodiscard]] bool ShouldAlarm(int track_id, bool matched, bool comparison_ready);
    void RetainTracks(const std::unordered_set<int>& active_track_ids);
    void Reset();

private:
    struct TrackState {
        size_t consecutive_non_matches{0};
        bool matched_seen{false};
    };

    RecognitionAlarmDecisionConfig config_;
    std::unordered_map<int, TrackState> track_states_;
};

}  // namespace cosmo
