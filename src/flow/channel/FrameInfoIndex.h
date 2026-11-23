#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>

namespace cosmo {
struct AlgFrameInfo {
    int64_t index{-1};
    int64_t streamIndex{-1};
    int64_t timestamp{-1};  // original packet-arrival wall time
    std::chrono::steady_clock::time_point firstTimePoint{};
    float fps{0.0f};
    // Append so existing five-field aggregate initialization remains valid.
    bool presentation_time_valid{false};
    int64_t presentation_timestamp_ms{0};
    int64_t presentation_steady_ms{0};
};

// Reordered output is keyed by its decoded packet index, not current input.
inline AlgFrameInfo TakeFrameInfo(std::deque<AlgFrameInfo>& pending, int64_t index) {
    AlgFrameInfo info;
    const auto it = std::find_if(pending.begin(), pending.end(),
                                 [index](const AlgFrameInfo& entry) { return entry.index == index; });
    if (it != pending.end()) {
        info = *it;
        pending.erase(it);
    }
    while (pending.size() > 300)
        pending.pop_front();
    return info;
}
}  // namespace cosmo
