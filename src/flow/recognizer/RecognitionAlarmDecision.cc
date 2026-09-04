#include "flow/recognizer/RecognitionAlarmDecision.h"

#include <algorithm>

namespace cosmo {

void RecognitionAlarmDecision::SetConfig(RecognitionAlarmDecisionConfig config) {
    config.stranger_confirm_count = std::max<size_t>(1, config.stranger_confirm_count);
    if (config.match_flag == config_.match_flag &&
        config.stranger_confirm_count == config_.stranger_confirm_count) {
        return;
    }
    config_ = config;
    Reset();
}

bool RecognitionAlarmDecision::ShouldAlarm(int track_id, bool matched, bool comparison_ready) {
    if (!comparison_ready) {
        return false;
    }

    if (config_.match_flag == MatchFlagType::Match) {
        return matched;
    }
    if (config_.match_flag == MatchFlagType::All) {
        return true;
    }

    auto& state = track_states_[track_id];
    if (matched) {
        state.matched_seen            = true;
        state.consecutive_non_matches = 0;
        return false;
    }
    if (state.matched_seen) {
        return false;
    }

    state.consecutive_non_matches += 1;
    return state.consecutive_non_matches >= config_.stranger_confirm_count;
}

void RecognitionAlarmDecision::RetainTracks(const std::unordered_set<int>& active_track_ids) {
    for (auto it = track_states_.begin(); it != track_states_.end();) {
        if (active_track_ids.count(it->first) == 0) {
            it = track_states_.erase(it);
        } else {
            ++it;
        }
    }
}

void RecognitionAlarmDecision::Reset() {
    track_states_.clear();
}

}  // namespace cosmo
