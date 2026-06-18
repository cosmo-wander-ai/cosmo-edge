#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace cosmo {

using TimerCallback = std::function<void()>;

// Opaque handle returned by Schedule(), used to cancel a task.
using TaskId = uint64_t;

// Sentinel value indicating no valid task.
inline constexpr TaskId kInvalidTaskId = 0;

struct TimerTaskEntry {
    TimerCallback callback;
    uint64_t next_run_time_ms;
    uint64_t period_ms;
    bool repeat;

    TimerTaskEntry(TimerCallback cb, uint64_t next_time, uint64_t period, bool r)
        : callback(std::move(cb)), next_run_time_ms(next_time), period_ms(period), repeat(r) {}
};

using TimerTaskMap = std::map<TaskId, TimerTaskEntry>;

class PeriodicTimer {
public:
    explicit PeriodicTimer(std::string name);

    ~PeriodicTimer();

    // Non-copyable, non-movable (owns a running thread).
    PeriodicTimer(const PeriodicTimer&)            = delete;
    PeriodicTimer& operator=(const PeriodicTimer&) = delete;

    // Start the periodic timer thread
    void Start();

    // Schedule a timer task with the given period (ms). Repeats by default.
    // Returns a TaskId handle for later cancellation.
    TaskId Schedule(TimerCallback callback, uint64_t period, bool repeat = true);

    // Cancel a previously scheduled timer task by its TaskId.
    void Cancel(TaskId id);

    // Stop the timer thread and clear all tasks.
    void Destroy();

protected:
    // Worker thread entry point.
    void Run();

private:
    std::atomic<bool> stop_{false};
    std::atomic<TaskId> next_id_{1};
    std::string name_;
    TimerTaskMap tasks_;

    std::mutex mtx_;
    std::condition_variable cv_;
    std::thread thread_;
};

using PeriodicTimerPtr = std::shared_ptr<PeriodicTimer>;

}  // namespace cosmo
