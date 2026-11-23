#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
// Historical per-task-group monotonic deadline selector; no catch-up burst.
template <class Queue>
class DeadlineSelector {
    struct State {
        std::int64_t period_ns;
        std::int64_t next_ns;
        std::int64_t last_seen_ns;
        std::uint64_t stream_generation;
    };
    std::map<std::string, State> states_;
    std::mutex mutex_;
    std::uint64_t calls_{0};

public:
    bool Allow(const std::string& group_id, const std::shared_ptr<Queue>& queue, std::int64_t now_ns,
               std::int64_t period_ns, std::uint64_t stream_generation = 0) {
        if (!queue || group_id.empty() || period_ns <= 0)
            return true;
        std::lock_guard<std::mutex> lock(mutex_);
        if ((++calls_ & 1023u) == 0) {
            for (auto i = states_.begin(); i != states_.end();) {
                if (now_ns - i->second.last_seen_ns > 60000000000LL)
                    i = states_.erase(i);
                else
                    ++i;
            }
        }
        const State current{period_ns, now_ns, now_ns, stream_generation};
        auto result = states_.try_emplace(group_id, current);
        auto& state = result.first->second;
        if (state.period_ns != period_ns || state.stream_generation != stream_generation)
            state = current;
        state.last_seen_ns = now_ns;
        if (now_ns < state.next_ns)
            return false;
        if (now_ns - state.next_ns >= period_ns)
            state.next_ns = now_ns + period_ns;
        else
            state.next_ns += period_ns;
        return true;
    }
};
