#pragma once
// Metadata-only trajectory selection: never retain pixels, native frames or stale features.
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
namespace cosmo::pedquality_candidate {
struct Candidate {
    std::string track;
    std::uint64_t frame;
    std::array<float, 6> channels;
};
class CurrentBest {
    struct Best {
        float score;
        std::uint64_t frame;
    };
    std::map<std::string, Best> best_;
    std::uint64_t generation_{0};

public:
    // Every candidate must be from this exact frame. A generation reset drops old
    // track IDs. Non-improving observations are not published as current frames.
    std::optional<std::vector<bool>> Select(std::uint64_t generation, std::uint64_t frame,
                                            const std::vector<Candidate>& items) {
        if (!generation || items.size() > 64)
            return {};
        std::set<std::string> active;
        for (const auto& item : items) {
            if (item.track.empty() || item.track.size() > 256 || item.frame != frame ||
                !active.insert(item.track).second)
                return {};
            for (float x : item.channels)
                if (!std::isfinite(x) || x < 0.f || x > 1.f)
                    return {};
        }
        if (generation_ != generation) {
            best_.clear();
            generation_ = generation;
        }
        for (const auto& item : items) {
            auto it = best_.find(item.track);
            if (it != best_.end() && frame <= it->second.frame)
                return {};
        }
        std::vector<bool> improved;
        improved.reserve(items.size());
        for (const auto& item : items) {
            const float score = item.channels[0] * 60.f + item.channels[4] * 40.f;
            auto it           = best_.find(item.track);
            const bool choose = it == best_.end() || score > it->second.score;
            improved.push_back(choose);
            if (it == best_.end())
                best_.emplace(item.track, Best{score, frame});
            else {
                if (choose)
                    it->second.score = score;
                it->second.frame = frame;
            }
        }
        for (auto it = best_.begin(); it != best_.end();)
            if (!active.count(it->first))
                it = best_.erase(it);
            else
                ++it;
        return improved;
    }
    void Reset() noexcept {
        best_.clear();
        generation_ = 0;
    }
};
}  // namespace cosmo::pedquality_candidate
