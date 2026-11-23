#pragma once
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <utility>

namespace cosmo::bench_ax {
// Benchmark-only equivalent of the old detector/classifier alternating queue.
// The granted task executes on its own model-owner thread; no SDK handle or
// owner is transferred. Callers must not hold NativeState or lifecycle locks
// while acquiring a turn.
class BenchTurnScheduler final {
    struct Waiter {
        bool ready{false};
        std::condition_variable cv;
    };
    std::mutex mutex_;
    std::deque<std::shared_ptr<Waiter>> detector_, classifier_;
    bool active_{false}, next_classifier_{false};
    void Grant() {
        if (active_ || (detector_.empty() && classifier_.empty()))
            return;
        const bool cls = detector_.empty() || (!classifier_.empty() && next_classifier_);
        auto& q        = cls ? classifier_ : detector_;
        auto item      = q.front();
        q.pop_front();
        active_          = true;
        next_classifier_ = !cls;
        item->ready      = true;
        item->cv.notify_one();
    }
    void Release() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        active_ = false;
        Grant();
    }

public:
    class Turn final {
        friend class BenchTurnScheduler;
        BenchTurnScheduler* scheduler_{};
        explicit Turn(BenchTurnScheduler* s) : scheduler_(s) {}

    public:
        Turn(const Turn&)            = delete;
        Turn& operator=(const Turn&) = delete;
        Turn(Turn&& other) noexcept : scheduler_(std::exchange(other.scheduler_, nullptr)) {}
        Turn& operator=(Turn&& other) noexcept {
            if (this != &other) {
                if (scheduler_)
                    scheduler_->Release();
                scheduler_ = std::exchange(other.scheduler_, nullptr);
            }
            return *this;
        }
        ~Turn() {
            if (scheduler_)
                scheduler_->Release();
        }
    };
#ifdef COSMO_BENCH_TURN_TEST
    std::pair<std::size_t, std::size_t> WaitingForTest() {
        std::lock_guard<std::mutex> lock(mutex_);
        return {detector_.size(), classifier_.size()};
    }
#endif
    Turn Acquire(bool classifier) {
        auto ticket = std::make_shared<Waiter>();
        std::unique_lock<std::mutex> lock(mutex_);
        auto& q = classifier ? classifier_ : detector_;
        q.push_back(ticket);
        Grant();
        try {
            ticket->cv.wait(lock, [&] { return ticket->ready; });
        } catch (...) {
            if (ticket->ready) {
                active_ = false;
                Grant();
            } else {
                for (auto i = q.begin(); i != q.end(); ++i)
                    if (*i == ticket) {
                        q.erase(i);
                        break;
                    }
            }
            throw;
        }
        return Turn(this);
    }
};
inline BenchTurnScheduler& BenchmarkTurns() {
    static auto* turns = new BenchTurnScheduler;
    return *turns;
}
inline bool BenchmarkTurnsEnabled() {
    const char* p = std::getenv("COSMO_AXERA_BENCH_TURN_SCHED");
    return p && std::strcmp(p, "1") == 0;
}
}  // namespace cosmo::bench_ax
