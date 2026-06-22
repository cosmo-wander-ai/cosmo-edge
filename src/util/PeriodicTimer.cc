// PeriodicTimer — Opaque handle returned by Schedule(), used to cancel a task.

#include "util/PeriodicTimer.h"

#include <vector>

#include "util/Log.h"
#include "util/TimeUtil.h"

namespace cosmo {

PeriodicTimer::PeriodicTimer(std::string name) : name_(std::move(name)) {}

PeriodicTimer::~PeriodicTimer() {
    if (!stop_) {
        Destroy();
    }
}

void PeriodicTimer::Start() {
    if (thread_.joinable()) {
        return;
    }
    stop_.store(false);
    thread_ = std::thread(&PeriodicTimer::Run, this);
}

TaskId PeriodicTimer::Schedule(TimerCallback callback, uint64_t period, bool repeat) {
    if (!callback)
        return kInvalidTaskId;
    TaskId id          = next_id_.fetch_add(1, std::memory_order_relaxed);
    uint64_t next_time = static_cast<uint64_t>(cosmo::util::GetMillisecondsFromSteadyClock()) + period;
    std::lock_guard<std::mutex> lck(mtx_);
    tasks_.emplace(id, TimerTaskEntry(std::move(callback), next_time, period, repeat));
    cv_.notify_all();
    return id;
}

void PeriodicTimer::Cancel(TaskId id) {
    if (id == kInvalidTaskId)
        return;
    std::lock_guard<std::mutex> lck(mtx_);
    tasks_.erase(id);
}

void PeriodicTimer::Destroy() {
    stop_.store(true);
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
    std::lock_guard<std::mutex> lck(mtx_);
    tasks_.clear();
}

void PeriodicTimer::Run() {
    std::unique_lock<std::mutex> lck(mtx_);
    while (!stop_.load()) {
        if (tasks_.empty()) {
            cv_.wait(lck, [this]() { return stop_.load() || !tasks_.empty(); });
            if (stop_.load())
                break;
        }

        uint64_t now         = static_cast<uint64_t>(cosmo::util::GetMillisecondsFromSteadyClock());
        uint64_t next_wakeup = UINT64_MAX;

        // Collect callbacks to invoke outside the lock.
        std::vector<TimerCallback> to_run;

        for (auto it = tasks_.begin(); it != tasks_.end();) {
            if (now >= it->second.next_run_time_ms) {
                to_run.push_back(it->second.callback);
                if (!it->second.repeat) {
                    it = tasks_.erase(it);
                    continue;
                }
                it->second.next_run_time_ms += it->second.period_ms;
            }

            uint64_t time_to_next = it->second.next_run_time_ms > now ? it->second.next_run_time_ms - now : 0;
            if (time_to_next < next_wakeup) {
                next_wakeup = time_to_next;
            }

            ++it;
        }

        if (!to_run.empty()) {
            lck.unlock();
            for (auto& cb : to_run) {
                if (cb) {
                    cb();
                }
            }
            lck.lock();
        } else if (next_wakeup != UINT64_MAX) {
            cv_.wait_for(lck, std::chrono::milliseconds(next_wakeup));
        }
    }
}

}  // namespace cosmo
